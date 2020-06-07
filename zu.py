import bpy
import bgl
import ext_zu as zu

def mat(m):
	return [c for v in m for c in v]

class ZuRenderEngine(bpy.types.RenderEngine):
	bl_idname = 'ZU'
	bl_label = 'Zu'
	bl_use_preview = True

	def __init__(self):
		self.scene = None
		self.objects = None

	def load_dg(self, dg):
		self.objects = {}
		for obj in dg.objects:
			if obj.type not in ['MESH', 'CURVE', 'SURFACE', 'META', 'FONT', 'VOLUME']:
				continue

			zu_obj = zu.obj_new(self.scene)

			mesh = obj.to_mesh()
			mesh.calc_loop_triangles()

			vertices = []
			for tri in mesh.loop_triangles:
				for i in tri.loops:
					loop = mesh.loops[i]
					vert = mesh.vertices[loop.vertex_index]
					vertices += list(vert.co)

			zu.obj_geom(zu_obj, vertices)
			zu.obj_transform(zu_obj, mat(obj.matrix_world))
			zu.obj_upload(zu_obj)
			self.objects[obj.name_full] = zu_obj

	def render(self, dg):
		scene = dg.scene
		scale = scene.render.resolution_percentage / 100.0
		width = scene.render.resolution_x * scale
		height = scene.render.resolution_y * scale

		# Create drawing target
		buf = bgl.Buffer(bgl.GL_UINT, 1)
		bgl.glGenFramebuffers(1, buf)
		fb = buf[0]
		bgl.glBindFramebuffer(bgl.GL_FRAMEBUFFER, fb)

		bgl.glGenTextures(1, buf)
		tex = buf[0]
		bgl.glBindTexture(bgl.GL_TEXTURE_2D, tex)
		bgl.glTexImage2D(bgl.GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_FLOAT);
		bgl.glTexParameteri(bgl.GL_TEXTURE_2D, bgl.GL_TEXTURE_MAG_FILTER, bgl.GL_NEAREST);
		bgl.glTexParameteri(bgl.GL_TEXTURE_2D, bgl.GL_TEXTURE_MIN_FILTER, bgl.GL_NEAREST);

		bgl.glFramebufferTexture(bgl.GL_FRAMEBUFFER, bgl.GL_COLOR_ATTACHMENT0, tex, 0);

		if bgl.glCheckFramebufferStatus(bgl.GL_FRAMEBUFFER) != bgl.GL_FRAMEBUFFER_COMPLETE:
			raise RuntimeError("Could not initialize GL framebuffer")

		# Prep Zu scene
		self.scene = zu.scene_new()
		# TODO: camera
		self.load_dg(dg)

		# Render
		bgl.glViewport(0,0,width,height)
		zu.scene_draw(self.scene, fb)

		# Retrieve texture data
		pixels = bgl.Buffer(bgl.GL_FLOAT, width*height*4)
		bgl.glReadPixels(0, 0, width, height, bgl.GL_RGBA, bgl.GL_FLOAT, pixels)

		# Clean up OpenGL stuff
		bgl.glBindFramebuffer(bgl.GL_FRAMEBUFFER, 0)
		buf[0] = tex
		bgl.glDeleteTextures(1, buf)
		buf[0] = fb
		bgl.glDeleteFramebuffers(1, buf)

		# Copy pixel data to output
		result = self.begin_result(0, 0, width, height)
		layer = result.layers[0].passes["Combined"]
		layer.rect = pixels
		self.end_result(result)

	def view_update(self, ctx, dg):
		if self.scene is None:
			self.scene = zu.scene_new()
			self.load_dg(dg)
		else:
			for update in dg.updates:
				pass

	def view_draw(self, ctx, dg):
		#region = ctx.region
		region3d = ctx.region_data
		self.bind_display_space_shader(dg.scene)

		buf = bgl.Buffer(bgl.GL_INT, 1)
		bgl.glGetIntegerv(bgl.GL_DRAW_FRAMEBUFFER_BINDING, buf)
		cam = region3d.perspective_matrix
		zu.scene_cam(self.scene, mat(cam))
		zu.scene_draw(self.scene, buf[0])

		self.unbind_display_space_shader()

def compatible_panels():
	incompatible_panels = {
	}

	for panel in bpy.types.Panel.__subclasses__():
		if hasattr(panel, 'COMPAT_ENGINES') and 'BLENDER_RENDER' in panel.COMPAT_ENGINES:
			if panel.__name__ not in incompatible_panels:
				yield panel

def register():
	bpy.utils.register_class(ZuRenderEngine)
	for panel in compatible_panels():
		panel.COMPAT_ENGINES.add(ZuRenderEngine.bl_idname);

def unregister():
	bpy.utils.unregister_class(ZuRenderEngine)
	for panel in compatible_panels():
		if ZuRenderEngine.bl_idname in panel.COMPAT_ENGINES:
			panel.COMPAT_ENGINES.remove(ZuRenderEngine.bl_idname);

if __name__=='__main__':
	register()
