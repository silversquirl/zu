#ifndef ZU_H
#define ZU_H

#include <GL/gl.h>

typedef GLfloat mat44[4*4];
typedef GLfloat vec3[3];

struct zu_scene {
	struct zu_obj **objects;
	size_t n_objects, a_objects;

	mat44 cam;
	GLuint vao, shader;
	struct {
		GLuint mvp;
	} uniform;

	// TODO: materials
	// TODO: textures
};

struct zu_scene *zu_scene_new(void);
void zu_scene_del(struct zu_scene *scene);
void zu_scene_draw(struct zu_scene *scene, GLuint fb);

struct zu_obj {
	struct zu_scene *scene;
	_Bool hide;

	mat44 transform; // Object space -> world space

	GLfloat *triangles;
	size_t n_triangles;
	GLuint vtx_buf;

	// TODO: object color
	// TODO: materials
	// TODO: UVs
	// TODO: vertex color
	// TODO: normals
	// TODO: show transparent
	// TODO: show in front
	// TODO: display type?
	// TODO: shader effects?
	// TODO: show all edges? Blender may do this for us
};

struct zu_obj *zu_obj_new(struct zu_scene *scene);
void zu_obj_del(struct zu_obj *obj);
GLfloat *zu_obj_geom(struct zu_obj *obj, size_t n_triangles);
int zu_obj_upload(struct zu_obj *obj);

#endif
