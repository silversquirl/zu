#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "zu.h"

#define SCENE_NAME "zu_scene"
#define OBJ_NAME "zu_obj"

static void scene_del(PyObject *pyscene) {
	struct zu_scene *scene = PyCapsule_GetPointer(pyscene, SCENE_NAME);
	if (!scene) return;
	zu_scene_del(scene);
}

static PyObject *scene_new(PyObject *self, PyObject *_) {
	struct zu_scene *scene = zu_scene_new();
	if (!scene) return PyErr_NoMemory();
	return PyCapsule_New(scene, SCENE_NAME, scene_del);
}

static PyObject *scene_cam(PyObject *self, PyObject *args) {
	PyObject *pyscene, *mat;
	if (!PyArg_ParseTuple(args, "OO", &pyscene, &mat)) return NULL;

	struct zu_scene *scene = PyCapsule_GetPointer(pyscene, SCENE_NAME);
	if (!scene) return NULL;

	Py_ssize_t mat_len = PyList_Size(mat);
	if (mat_len < 0) return NULL;

	if (mat_len != 4*4) {
		PyErr_Format(PyExc_ValueError, "Length of `mat' should be 16, got %d", mat_len);
		return NULL;
	}

	for (Py_ssize_t i = 0; i < mat_len; i++) {
		PyObject *item = PyList_GET_ITEM(mat, i);
		double value = PyFloat_AsDouble(item);
		if (value == -1.0 && PyErr_Occurred()) return NULL;
		scene->cam[i] = value;
	}

	return Py_None;
}

static PyObject *scene_draw(PyObject *self, PyObject *args) {
	PyObject *pyscene;
	GLuint fb;
	if (!PyArg_ParseTuple(args, "OI", &pyscene, &fb)) return NULL;
	struct zu_scene *scene = PyCapsule_GetPointer(pyscene, SCENE_NAME);
	if (!scene) return NULL;

	zu_scene_draw(scene, fb);

	return Py_None;
}

static void obj_del(PyObject *pyobj) {
	struct zu_obj *obj = PyCapsule_GetPointer(pyobj, OBJ_NAME);
	if (!obj) return;
	zu_obj_del(obj);
}

static PyObject *obj_new(PyObject *self, PyObject *pyscene) {
	struct zu_scene *scene = PyCapsule_GetPointer(pyscene, SCENE_NAME);
	if (!scene) return NULL;

	struct zu_obj *obj = zu_obj_new(scene);
	if (!obj) return PyErr_NoMemory();

	return PyCapsule_New(obj, OBJ_NAME, obj_del);
}

static PyObject *obj_transform(PyObject *self, PyObject *args) {
	PyObject *pyobj, *mat;
	if (!PyArg_ParseTuple(args, "OO", &pyobj, &mat)) return NULL;

	struct zu_obj *obj = PyCapsule_GetPointer(pyobj, OBJ_NAME);
	if (!obj) return NULL;

	Py_ssize_t mat_len = PyList_Size(mat);
	if (mat_len < 0) return NULL;

	if (mat_len != 4*4) {
		PyErr_Format(PyExc_ValueError, "Length of `mat' should be 16, got %d", mat_len);
		return NULL;
	}

	for (Py_ssize_t i = 0; i < mat_len; i++) {
		PyObject *item = PyList_GET_ITEM(mat, i);
		double value = PyFloat_AsDouble(item);
		if (value == -1.0 && PyErr_Occurred()) return NULL;
		obj->transform[i] = value;
	}

	return Py_None;
}

static PyObject *obj_geom(PyObject *self, PyObject *args) {
	PyObject *pyobj, *verts;
	if (!PyArg_ParseTuple(args, "OO", &pyobj, &verts)) return NULL;

	struct zu_obj *obj = PyCapsule_GetPointer(pyobj, OBJ_NAME);
	if (!obj) return NULL;

	Py_ssize_t verts_len = PyList_Size(verts);
	if (verts_len < 0) return NULL;

	// 3 vertices per triangle, 3 components per vertex
	if (verts_len % 9) {
		PyErr_SetString(PyExc_ValueError, "Length of `verts' must be a multiple of 9");
		return NULL;
	}

	GLfloat *buf = zu_obj_geom(obj, verts_len/9);

	for (Py_ssize_t i = 0; i < verts_len; i++) {
		PyObject *item = PyList_GET_ITEM(verts, i);
		double value = PyFloat_AsDouble(item);
		if (value == -1.0 && PyErr_Occurred()) return NULL;
		buf[i] = value;
	}

	return Py_None;
}

static PyObject *obj_color(PyObject *self, PyObject *args) {
	PyObject *pyobj, *color;
	if (!PyArg_ParseTuple(args, "OO", &pyobj, &color)) return NULL;

	struct zu_obj *obj = PyCapsule_GetPointer(pyobj, OBJ_NAME);
	if (!obj) return NULL;

	Py_ssize_t color_len = PyList_Size(color);
	if (color_len < 0) return NULL;

	if (color_len != 4) {
		PyErr_Format(PyExc_ValueError, "Length of `color' should be 4, got %d", color_len);
		return NULL;
	}

	for (Py_ssize_t i = 0; i < color_len; i++) {
		PyObject *item = PyList_GET_ITEM(color, i);
		double value = PyFloat_AsDouble(item);
		if (value == -1.0 && PyErr_Occurred()) return NULL;
		obj->color[i] = value;
	}

	return Py_None;
}

static PyObject *obj_hide(PyObject *self, PyObject *pyobj) {
	struct zu_obj *obj = PyCapsule_GetPointer(pyobj, OBJ_NAME);
	if (!obj) return NULL;

	obj->hide = 1;

	return Py_None;
}

static PyObject *obj_upload(PyObject *self, PyObject *pyobj) {
	struct zu_obj *obj = PyCapsule_GetPointer(pyobj, OBJ_NAME);
	if (!obj) return NULL;

	if (zu_obj_upload(obj)) {
		PyErr_SetString(PyExc_RuntimeError, "Failed to allocate OpenGL buffer");
		return NULL;
	}

	return Py_None;
}

// Blender stuff that isn't wrapped in bpy
void DRW_opengl_context_enable(void);
void DRW_opengl_context_disable(void);

static PyObject *blen_gl_enable(PyObject *self, PyObject *_) {
	DRW_opengl_context_enable();
	return Py_None;
}

static PyObject *blen_gl_disable(PyObject *self, PyObject *_) {
	DRW_opengl_context_disable();
	return Py_None;
}

PyMethodDef methods[] = {
	{"scene_new", scene_new, METH_NOARGS, PyDoc_STR("Create a new Zu scene")},
	{"scene_cam", scene_cam, METH_VARARGS, PyDoc_STR("Set the camera matrix of a Zu scene")},
	{"scene_draw", scene_draw, METH_VARARGS, PyDoc_STR("Draw to the specified OpenGL framebuffer")},

	{"obj_new", obj_new, METH_O, PyDoc_STR("Create a new Zu object linked to the specified scene")},
	{"obj_transform", obj_transform, METH_VARARGS, PyDoc_STR("Set the transformation of a Zu object")},
	{"obj_geom", obj_geom, METH_VARARGS, PyDoc_STR("Set the geometry of a Zu object. Takes a Zu object and a list of floats. Each sequence of 9 floats represents one triangle")},
	{"obj_color", obj_color, METH_VARARGS, PyDoc_STR("Set the object color of a Zu object")},
	{"obj_hide", obj_hide, METH_O, PyDoc_STR("Hide a Zu object from the render. This is provided because there is no way to safely delete an object from Python")},
	{"obj_upload", obj_upload, METH_O, PyDoc_STR("Upload a Zu object to the GPU")},

	{"blen_gl_enable", blen_gl_enable, METH_NOARGS, PyDoc_STR("Enable the Blender OpenGL context")},
	{"blen_gl_disable", blen_gl_disable, METH_NOARGS, PyDoc_STR("Disable the Blender OpenGL context")},

	{0},
};

PyModuleDef module = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = "zu",
	.m_doc = PyDoc_STR("Low-level Python interface to Zu"),
	.m_size = 0,
	.m_methods = methods,
	0,
};

PyObject *PyInit_ext_zu(void) {
	return PyModule_Create(&module);
}
