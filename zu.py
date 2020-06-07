import bpy
import bgl
import ext_zu as zu

def mat(m):
	return [c for v in m for c in v]

def glerr(msg="OpenGL error"):
	err = bgl.glGetError()
	if err == 0:
		return

	glmsg = {
		bgl.GL_INVALID_ENUM: "invalid enum",
		bgl.GL_INVALID_VALUE: "invalid value",
		bgl.GL_INVALID_OPERATION: "invalid operation",
		bgl.GL_INVALID_FRAMEBUFFER_OPERATION: "invalid framebuffer operation",
		bgl.GL_OUT_OF_MEMORY: "out of memory",
	}.get(err, "unknown ({:x})".format(err))
	raise RuntimeError(msg + ": " + glmsg)

def obj_has_mesh(obj):
	return obj.type in ['MESH', 'CURVE', 'SURFACE', 'META', 'FONT', 'VOLUME']

def update_obj_geom(zu_obj, bl_obj):
	mesh = bl_obj.to_mesh()
	mesh.calc_loop_triangles()

	vertices = []
	for tri in mesh.loop_triangles:
		for i in tri.loops:
			loop = mesh.loops[i]
			vert = mesh.vertices[loop.vertex_index]
			vertices += list(vert.co)

	zu.obj_geom(zu_obj, vertices)

class ZuRenderEngine(bpy.types.RenderEngine):
	bl_idname = 'ZU'
	bl_label = 'Zu'
	bl_use_preview = True

	def __init__(self):
		self.scene = None
		self.objects = None

	def render(self, dg):
		scene = dg.scene
		scale = scene.render.resolution_percentage / 100.0
		width = int(scene.render.resolution_x * scale)
		height = int(scene.render.resolution_y * scale)

		zu.blen_gl_enable()

		try:
			# Create drawing target
			buf = bgl.Buffer(bgl.GL_INT, 1)
			bgl.glGenFramebuffers(1, buf)
			fb = buf[0]
			bgl.glBindFramebuffer(bgl.GL_FRAMEBUFFER, fb)

			bgl.glGenTextures(1, buf)
			tex = buf[0]
			bgl.glBindTexture(bgl.GL_TEXTURE_RECTANGLE, tex)
			bgl.glTexImage2D(bgl.GL_TEXTURE_RECTANGLE, 0, bgl.GL_RGBA, width, height, 0, bgl.GL_RGBA, bgl.GL_FLOAT, None);
			bgl.glTexParameteri(bgl.GL_TEXTURE_RECTANGLE, bgl.GL_TEXTURE_MAG_FILTER, bgl.GL_NEAREST);
			bgl.glTexParameteri(bgl.GL_TEXTURE_RECTANGLE, bgl.GL_TEXTURE_MIN_FILTER, bgl.GL_NEAREST);

			bgl.glFramebufferTexture(bgl.GL_FRAMEBUFFER, bgl.GL_COLOR_ATTACHMENT0, tex, 0);

			status = bgl.glCheckFramebufferStatus(bgl.GL_FRAMEBUFFER)
			if status == 0:
				glerr("Could not get framebuffer status")
			elif status != bgl.GL_FRAMEBUFFER_COMPLETE:
				msg = {
					bgl.GL_FRAMEBUFFER_UNDEFINED: "undefined",
					bgl.GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: "incomplete attachment",
					bgl.GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: "missing attachment",
					bgl.GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: "incomplete draw buffer",
					bgl.GL_FRAMEBUFFER_UNSUPPORTED: "unsupported",
					bgl.GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: "incomplete multisample",
					bgl.GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS: "incomplete layer targets",
				}.get(status, "unknown ({:x})".format(status))
				raise RuntimeError("Could not initialize GL framebuffer: " + msg)

			# Prep Zu scene
			self.scene = zu.scene_new()
			view_mat = self.camera_model_matrix(scene.camera)
			view_mat.invert()
			win_mat = scene.camera.calc_matrix_camera(dg, x=width, y=height)
			cam = win_mat @ view_mat
			zu.scene_cam(self.scene, mat(cam))
			self.load_dg(dg)

			# Render
			bgl.glViewport(0,0,width,height)
			zu.scene_draw(self.scene, fb)

			# Retrieve texture data
			pixels = bgl.Buffer(bgl.GL_FLOAT, width*height*4)
			bgl.glReadPixels(0, 0, width, height, bgl.GL_RGBA, bgl.GL_FLOAT, pixels)

			# Clean up OpenGL stuff
			buf[0] = tex
			bgl.glDeleteTextures(1, buf)
			buf[0] = fb
			bgl.glDeleteFramebuffers(1, buf)

			# Copy pixel data to output
			result = self.begin_result(0, 0, width, height)
			layer = result.layers[0].passes["Combined"]
			pixels = pixels.to_list()
			layer.rect = list(zip(*[pixels[i::4] for i in range(4)])) # FIXME: holy fuck this is slow
			self.end_result(result)

		finally:
			zu.blen_gl_disable()
			self.scene = None
			self.objects = None

	def load_dg(self, dg):
		self.objects = {}
		for obj in dg.objects:
			if not obj_has_mesh(obj):
				continue

			zu_obj = zu.obj_new(self.scene)

			update_obj_geom(zu_obj, obj)
			zu.obj_upload(zu_obj)
			zu.obj_transform(zu_obj, mat(obj.matrix_world))
			zu.obj_color(zu_obj, list(obj.color))
			self.objects[obj.name_full] = zu_obj

	def view_update(self, ctx, dg):
		if self.scene is None:
			self.scene = zu.scene_new()
			self.load_dg(dg)
		else:
			for update in dg.updates:
				if not isinstance(update.id, bpy.types.Object):
					continue

				if not obj_has_mesh(update.id):
					continue

				if update.id.name_full not in self.objects:
					self.objects[update.id.name_full] = zu.obj_new(self.scene)

				zu_obj = self.objects[update.id.name_full]

				if update.is_updated_geometry:
					update_obj_geom(zu_obj, update.id)
					zu.obj_upload(zu_obj)

				if update.is_updated_transform:
					zu.obj_transform(zu_obj, mat(update.id.matrix_world))

				zu.obj_color(zu_obj, list(update.id.color))

	def view_draw(self, ctx, dg):
		if self.scene is None:
			return

		buf = bgl.Buffer(bgl.GL_INT, 1)
		bgl.glGetIntegerv(bgl.GL_DRAW_FRAMEBUFFER_BINDING, buf)
		cam = ctx.region_data.perspective_matrix
		zu.scene_cam(self.scene, mat(cam))
		zu.scene_draw(self.scene, buf[0])

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
