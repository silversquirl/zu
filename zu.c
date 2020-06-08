#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/glew.h>

#include "zu.h"

const mat44 mat44_id = {
	1,0,0,0,
	0,1,0,0,
	0,0,1,0,
	0,0,0,1,
};

// Can't assign matrices/vectors normally, so we need special macros for it
#define set(dest, src) memcpy(dest, src, sizeof (src)) // Generic
#define setm4(dest, ...) memcpy(dest, (mat44)__VA_ARGS__, sizeof (mat44)) // mat44 literal
#define setv3(dest, ...) memcpy(dest, (vec3)__VA_ARGS__, sizeof (vec3)) // vec3 literal
#define setv4(dest, ...) memcpy(dest, (vec4)__VA_ARGS__, sizeof (vec4)) // vec4 literal

static void matmul(mat44 m, mat44 a, mat44 b) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			m[i+4*j] = 0;
			for (int k = 0; k < 4; k++) {
				m[i+4*j] += a[4*i+k] * b[4*k+j];
			}
		}
	}
}

// Shaders {{{
const char *vshad_obj_clr =
	"#version 330 core\n"
	"layout(location = 0) in vec3 vert;\n"
	"uniform mat4 mvp;\n"
	"uniform vec4 obj_clr;\n"
	"out vec4 frag_clr;\n"
	"\n"
	"void main() {\n"
	"	gl_Position = mvp * vec4(vert, 1);\n"
	"	frag_clr = obj_clr;\n"
	"}\n";

const char *vshad_vert_clr =
	"#version 330 core\n"
	"layout(location = 0) in vec3 vert;\n"
	"layout(location = 1) in vec4 vert_clr;\n"
	"uniform mat4 mvp;\n"
	"out vec4 frag_clr;\n"
	"\n"
	"void main() {\n"
	"	gl_Position = mvp * vec4(vert, 1);\n"
	"	frag_clr = vert_clr;\n"
	"}\n";

const char *fshad =
	"#version 330 core\n"
	"in vec4 frag_clr;\n"
	"out vec4 color;\n"
	"\n"
	"void main() {\n"
	"	color = frag_clr;\n"
	"}\n";

static GLuint compile_shader(GLuint shader, const char *src) {
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);

	GLint result, log_len;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);

	if (log_len > 0) {
		char buf[log_len];
		glGetShaderInfoLog(shader, log_len, NULL, buf);
		fputs(buf, stderr);
		fputc('\n', stderr);
	}

	return result;
}

static GLuint load_shaders(const char *vert_src, const char *frag_src) {
	GLuint vert = glCreateShader(GL_VERTEX_SHADER);
	if (!compile_shader(vert, vert_src)) return 0;
	GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
	if (!compile_shader(frag, frag_src)) return 0;

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vert);
	glAttachShader(prog, frag);
	glLinkProgram(prog);

	GLint result, log_len;
	glGetProgramiv(prog, GL_LINK_STATUS, &result);
	glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &log_len);

	if (log_len > 0) {
		char buf[log_len];
		glGetProgramInfoLog(prog, log_len, NULL, buf);
		fputs(buf, stderr);
		fputc('\n', stderr);
	}

	glDetachShader(prog, vert);
	glDetachShader(prog, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	if (!result) {
		glDeleteProgram(prog);
		return 0;
	}

	return prog;
}
// }}}

#define ZU_A_OBJ_INIT 8

struct zu_scene *zu_scene_new(void) {
	struct zu_scene *scene = malloc(sizeof *scene);
	if (!scene) return NULL;

	scene->objects = NULL;
	scene->n_objects = 0;
	scene->a_objects = 0;

	set(scene->cam, mat44_id);
	glGenVertexArrays(1, &scene->vao);

	scene->shader.obj_clr.id = load_shaders(vshad_obj_clr, fshad);
	if (!scene->shader.obj_clr.id) {
		free(scene);
		return NULL;
	}

	scene->shader.vert_clr.id = load_shaders(vshad_vert_clr, fshad);
	if (!scene->shader.vert_clr.id) {
		glDeleteProgram(scene->shader.obj_clr.id);
		free(scene);
		return NULL;
	}

	scene->shader.obj_clr.mvp = glGetUniformLocation(scene->shader.obj_clr.id, "mvp");
	scene->shader.obj_clr.obj_clr = glGetUniformLocation(scene->shader.obj_clr.id, "obj_clr");

	scene->shader.vert_clr.mvp = glGetUniformLocation(scene->shader.vert_clr.id, "mvp");

	return scene;
}

void zu_scene_del(struct zu_scene *scene) {
	free(scene);
}

// TODO: zu_scene_minify - shrinks all buffers to minimize the memory consumption of the scene
// Also should test whether reallocating to move the buffers closer together affects performance

enum {ZU_VERT_ARRAYS = 2};

void zu_scene_draw(struct zu_scene *scene, GLuint fb) {
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb);

	// TODO: maybe only do this once
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	glBindVertexArray(scene->vao);

	glClearColor(0,0,0,1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	for (int i = 0; i < ZU_VERT_ARRAYS; i++) glEnableVertexAttribArray(i);
	for (size_t i = 0; i < scene->n_objects; i++) {
		struct zu_obj *obj = scene->objects[i];

		mat44 mvp;
		matmul(mvp, scene->cam, obj->transform);

		if (obj->vert_clr) {
			glUseProgram(scene->shader.vert_clr.id);
			glUniformMatrix4fv(scene->shader.vert_clr.mvp, 1, GL_FALSE, mvp);

			glBindBuffer(GL_ARRAY_BUFFER, obj->vert_clr_buf);
			glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, 0);
		} else {
			glUseProgram(scene->shader.obj_clr.id);
			glUniformMatrix4fv(scene->shader.obj_clr.mvp, 1, GL_FALSE, mvp);
			glUniform4fv(scene->shader.obj_clr.obj_clr, 1, obj->color);
		}

		glBindBuffer(GL_ARRAY_BUFFER, obj->vert_buf);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

		glDrawArrays(GL_TRIANGLES, 0, 3 * obj->n_triangles);
	}
	for (int i = 0; i < ZU_VERT_ARRAYS; i++) glDisableVertexAttribArray(i);

	glDisable(GL_DEPTH_TEST);
}

struct zu_obj *zu_obj_new(struct zu_scene *scene) {
	if (scene->n_objects >= scene->a_objects) {
		size_t a_objects = scene->a_objects * 2;
		if (!a_objects) a_objects = ZU_A_OBJ_INIT;

		struct zu_obj **objects = realloc(scene->objects, a_objects * sizeof *objects);
		if (!objects) return NULL;

		scene->objects = objects;
		scene->a_objects = a_objects;
	}

	struct zu_obj *obj = malloc(sizeof *obj);
	if (!obj) return NULL;
	scene->objects[scene->n_objects++] = obj;

	obj->scene = scene;
	obj->hide = 0;

	set(obj->transform, mat44_id);
	setv4(obj->color, {1,1,1,1});

	obj->n_triangles = 0;
	obj->vert = NULL;
	obj->vert_clr = NULL;

	obj->vert_buf = obj->vert_clr_buf = 0;

	return obj;
}

void zu_obj_del(struct zu_obj *obj) {
	struct zu_scene *scene = obj->scene;

	if (obj->vert_buf) glDeleteBuffers(1, &obj->vert_buf);
	if (obj->vert) free(obj->vert);
	if (obj->vert_clr_buf) glDeleteBuffers(1, &obj->vert_clr_buf);
	if (obj->vert_clr) free(obj->vert_clr);
	free(obj);

	scene->n_objects--;
	struct zu_obj **p = scene->objects + scene->n_objects, *prev = NULL, *tmp;
	while (--p >= scene->objects) {
		tmp = *p;
		*p = prev;
		prev = tmp;

		if (prev == obj) break;
	}
}

// n_triangles * 3 vertices * n components * GLfloat
#define obj_buflen(obj, components) ((obj)->n_triangles * 3 * components * sizeof (GLfloat))

GLfloat *zu_obj_geom(struct zu_obj *obj, size_t n_triangles) {
	if (obj->vert) free(obj->vert);
	if (obj->vert_clr) {
		free(obj->vert_clr);
		obj->vert_clr = NULL;
	}

	obj->n_triangles = n_triangles;
	return obj->vert = malloc(obj_buflen(obj, 3));
}

GLfloat *zu_obj_vert_clr(struct zu_obj *obj) {
	if (obj->vert_clr) free(obj->vert_clr);
	return obj->vert_clr = malloc(obj_buflen(obj, 4));
}

int zu_obj_upload(struct zu_obj *obj) {
	glBindVertexArray(obj->scene->vao);

#define obj_upload_buf(name, components) do { \
	if (obj->name) { \
		if (!obj->name##_buf) { \
			glGenBuffers(1, &obj->name##_buf); \
			if (!obj->name##_buf) return 1; \
		} \
		\
		glBindBuffer(GL_ARRAY_BUFFER, obj->name##_buf); \
		glBufferData(GL_ARRAY_BUFFER, obj_buflen(obj, components), obj->name, GL_STATIC_DRAW); \
	} \
} while (0)

	obj_upload_buf(vert, 3);
	obj_upload_buf(vert_clr, 4);

	return 0;
}
