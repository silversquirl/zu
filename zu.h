#ifndef ZU_H
#define ZU_H

#include <GL/gl.h>

typedef GLfloat mat44[4*4];
typedef GLfloat vec3[3];
typedef GLfloat vec4[4];

struct zu_scene {
	struct zu_obj **objects;
	size_t n_objects, a_objects;

	mat44 cam;
	GLuint vao;

	struct {
		struct {
			GLuint id;
			GLint mvp, obj_clr;
		} obj_clr;

		struct {
			GLuint id;
			GLint mvp;
		} vert_clr;
	} shader;

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
	vec4 color;

	size_t n_triangles;
	GLfloat *vert;
	GLfloat *vert_clr;
	GLuint vert_buf, vert_clr_buf;

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
GLfloat *zu_obj_vert_clr(struct zu_obj *obj);
int zu_obj_upload(struct zu_obj *obj);

#endif
