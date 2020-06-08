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

	vert = []
	if mesh.vertex_colors.active is not None:
		vert_clr = []
	else:
		vert_clr = None

	for tri in mesh.loop_triangles:
		for i in tri.loops:
			loop = mesh.loops[i]

			vert += list(mesh.vertices[loop.vertex_index].co)

			if vert_clr is not None:
				color = mesh.vertex_colors.active.data[i]
				vert_clr += list(color.color)

	zu.obj_geom(zu_obj, vert)
	if vert_clr is not None:
		zu.obj_vert_clr(zu_obj, vert_clr)
	zu.obj_upload(zu_obj)

class ZuRenderEngine(bpy.types.RenderEngine):
	bl_idname = 'ZU'
	bl_label = 'Zu'
	bl_use_preview = True

	def __init__(self):
		self.scene = None
		self.objects = None

		self.vtx_buf = None
		self.vao = None

		self.tex = None
		self.depth = None
		self.fb = None
		self.dim = None

	def __del__(self):
		self.delfb()
		del self.objects
		del self.scene

	def genfb(self, width, height):
		buf = bgl.Buffer(bgl.GL_INT, 1)
		bgl.glGenFramebuffers(1, buf)
		self.fb = buf[0]
		bgl.glBindFramebuffer(bgl.GL_FRAMEBUFFER, self.fb)

		bgl.glGenTextures(1, buf)
		self.tex = buf[0]
		bgl.glActiveTexture(bgl.GL_TEXTURE0)
		bgl.glBindTexture(bgl.GL_TEXTURE_2D, self.tex)
		bgl.glTexImage2D(bgl.GL_TEXTURE_2D, 0, bgl.GL_RGBA16F, width, height, 0, bgl.GL_RGBA, bgl.GL_FLOAT, None)
		bgl.glTexParameteri(bgl.GL_TEXTURE_2D, bgl.GL_TEXTURE_MAG_FILTER, bgl.GL_NEAREST)
		bgl.glTexParameteri(bgl.GL_TEXTURE_2D, bgl.GL_TEXTURE_MIN_FILTER, bgl.GL_NEAREST)
		bgl.glFramebufferTexture(bgl.GL_FRAMEBUFFER, bgl.GL_COLOR_ATTACHMENT0, self.tex, 0);

		bgl.glGenRenderbuffers(1, buf)
		self.depth = buf[0]
		bgl.glBindRenderbuffer(bgl.GL_RENDERBUFFER, self.depth)
		bgl.glRenderbufferStorage(bgl.GL_RENDERBUFFER, bgl.GL_DEPTH_COMPONENT, width, height)
		bgl.glFramebufferRenderbuffer(bgl.GL_FRAMEBUFFER, bgl.GL_DEPTH_ATTACHMENT, bgl.GL_RENDERBUFFER, self.depth)

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

	def delfb(self):
		if not hasattr(self, "fb"):
			return

		buf = bgl.Buffer(bgl.GL_INT, 1)

		if self.vtx_buf is not None:
			bgl.glDeleteBuffers(2, self.vtx_buf)
			self.vtx_buf = None
		if self.vao is not None:
			buf[0] = self.vao
			bgl.glDeleteVertexArrays(1, buf)
			self.vao = None

		if self.tex is not None:
			buf[0] = self.tex
			bgl.glDeleteTextures(1, buf)
			self.tex = None
		if self.depth is not None:
			buf[0] = self.depth
			bgl.glDeleteRenderbuffers(1, buf)
			self.depth = None
		if self.fb is not None:
			buf[0] = self.fb
			bgl.glDeleteFramebuffers(1, buf)
			self.fb = None

	def render(self, dg):
		scene = dg.scene
		scale = scene.render.resolution_percentage / 100.0
		width = int(scene.render.resolution_x * scale)
		height = int(scene.render.resolution_y * scale)

		zu.blen_gl_enable()

		try:
			# Create drawing target
			self.genfb(width, height)

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
			zu.scene_draw(self.scene, self.fb)

			# Retrieve texture data
			pixels = bgl.Buffer(bgl.GL_FLOAT, width*height*4)
			bgl.glReadPixels(0, 0, width, height, bgl.GL_RGBA, bgl.GL_FLOAT, pixels)

			# Copy pixel data to output
			result = self.begin_result(0, 0, width, height)
			layer = result.layers[0].passes["Combined"]
			pixels = pixels.to_list()
			layer.rect = list(zip(*[pixels[i::4] for i in range(4)])) # FIXME: holy fuck this is slow
			self.end_result(result)

		finally:
			del self.objects
			self.objects = None
			del self.scene
			self.scene = None
			bgl.glBindFramebuffer(bgl.GL_FRAMEBUFFER, 0)
			self.delfb()
			zu.blen_gl_disable()

	def load_dg(self, dg):
		self.objects = {}
		for obj in dg.objects:
			if not obj_has_mesh(obj):
				continue

			zu_obj = zu.obj_new(self.scene)

			update_obj_geom(zu_obj, obj)
			zu.obj_transform(zu_obj, mat(obj.matrix_world))
			zu.obj_color(zu_obj, list(obj.color))
			self.objects[obj.name_full] = zu_obj

	def prepare_viewport(self, ctx, dg):
		width, height = ctx.region.width, ctx.region.height
		self.dim = (width, height)

		buf = bgl.Buffer(bgl.GL_INT, 1)
		bgl.glGetIntegerv(bgl.GL_DRAW_FRAMEBUFFER_BINDING, buf)
		fb = buf[0]

		self.genfb(width, height)

		bgl.glGenVertexArrays(1, buf)
		self.vao = buf[0]
		bgl.glBindVertexArray(self.vao)

		quad = bgl.Buffer(bgl.GL_FLOAT, 2*4, [0, 0, width, 0, width, height, 0, height])
		uv = bgl.Buffer(bgl.GL_FLOAT, 2*4, [0, 0, 1, 0, 1, 1, 0, 1])

		self.bind_display_space_shader(dg.scene)
		bgl.glGetIntegerv(bgl.GL_CURRENT_PROGRAM, buf)
		self.unbind_display_space_shader()
		self.quad_in = bgl.glGetAttribLocation(buf[0], "pos")
		self.uv_in = bgl.glGetAttribLocation(buf[0], "texCoord")

		bgl.glEnableVertexAttribArray(self.quad_in)
		bgl.glEnableVertexAttribArray(self.uv_in)

		self.vtx_buf = bgl.Buffer(bgl.GL_INT, 2)
		bgl.glGenBuffers(2, self.vtx_buf)
		bgl.glBindBuffer(bgl.GL_ARRAY_BUFFER, self.vtx_buf[0])
		bgl.glBufferData(bgl.GL_ARRAY_BUFFER, 2*4*4, quad, bgl.GL_STATIC_DRAW)
		bgl.glVertexAttribPointer(self.quad_in, 2, bgl.GL_FLOAT, bgl.GL_FALSE, 0, None)

		bgl.glBindBuffer(bgl.GL_ARRAY_BUFFER, self.vtx_buf[1])
		bgl.glBufferData(bgl.GL_ARRAY_BUFFER, 2*4*4, uv, bgl.GL_STATIC_DRAW)
		bgl.glVertexAttribPointer(self.uv_in, 2, bgl.GL_FLOAT, bgl.GL_FALSE, 0, None)

		bgl.glDisableVertexAttribArray(self.quad_in)
		bgl.glDisableVertexAttribArray(self.uv_in)

		bgl.glBindFramebuffer(bgl.GL_FRAMEBUFFER, fb)

	def view_update(self, ctx, dg):
		if self.scene is None:
			self.scene = zu.scene_new()
			self.prepare_viewport(ctx, dg)
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

				if update.is_updated_transform:
					zu.obj_transform(zu_obj, mat(update.id.matrix_world))

				zu.obj_color(zu_obj, list(update.id.color))
		self.view_draw(ctx, dg)

	def view_draw(self, ctx, dg):
		if self.scene is None:
			return

		buf = bgl.Buffer(bgl.GL_INT, 1)
		bgl.glGetIntegerv(bgl.GL_DRAW_FRAMEBUFFER_BINDING, buf)
		if buf[0] == 0:
			return

		width, height = ctx.region.width, ctx.region.height
		if self.dim != (width, height):
			self.delfb()
			self.prepare_viewport(ctx, dg)

		# Render the scene
		bgl.glViewport(0, 0, width, height)
		cam = ctx.region_data.perspective_matrix
		zu.scene_cam(self.scene, mat(cam))
		zu.scene_draw(self.scene, self.fb)

		# Copy the rendered scene to the viewport (through the color space adjustment shader)
		bgl.glBindFramebuffer(bgl.GL_FRAMEBUFFER, buf[0])
		bgl.glEnable(bgl.GL_BLEND)
		bgl.glBlendFunc(bgl.GL_SRC_ALPHA, bgl.GL_ONE_MINUS_SRC_ALPHA)

		self.bind_display_space_shader(dg.scene)
		bgl.glBindVertexArray(self.vao)
		bgl.glEnableVertexAttribArray(self.quad_in)
		bgl.glEnableVertexAttribArray(self.uv_in)

		bgl.glActiveTexture(bgl.GL_TEXTURE0)
		bgl.glBindTexture(bgl.GL_TEXTURE_2D, self.tex)
		bgl.glDrawArrays(bgl.GL_TRIANGLE_FAN, 0, 4)

		bgl.glBindTexture(bgl.GL_TEXTURE_2D, 0)
		bgl.glDisableVertexAttribArray(self.quad_in)
		bgl.glDisableVertexAttribArray(self.uv_in)
		bgl.glBindVertexArray(0)

		self.unbind_display_space_shader()
		bgl.glDisable(bgl.GL_BLEND)

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
