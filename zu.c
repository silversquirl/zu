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
#define setm(dest, src) memcpy(dest, (mat44)src, sizeof (mat44)) // mat44 literal
#define setv(dest, src) memcpy(dest, (vec3)src, sizeof (vec3)) // vec3 literal

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
const char *vert_shader =
	"#version 330 core\n"
	"layout(location = 0) in vec3 vert;\n"
	"uniform mat4 mvp;\n"
	"\n"
	"void main() {\n"
	"	gl_Position = mvp * vec4(vert, 1);\n"
	"}\n";

const char *frag_shader =
	"#version 330 core\n"
	"out vec3 color;\n"
	"\n"
	"void main() {\n"
	"	color = vec3(1,1,1);\n"
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
	glGetProgramiv(prog, GL_COMPILE_STATUS, &result);
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
	GLuint shader = load_shaders(vert_shader, frag_shader);
	if (!shader) return NULL;

	struct zu_scene *scene = malloc(sizeof *scene);
	if (!scene) return NULL;

	scene->objects = NULL;
	scene->n_objects = 0;
	scene->a_objects = 0;

	set(scene->cam, mat44_id);
	glGenVertexArrays(1, &scene->vao);
	scene->shader = shader;
	scene->uniform.mvp = glGetUniformLocation(scene->shader, "mvp");

	return scene;
}

void zu_scene_del(struct zu_scene *scene) {
	free(scene);
}

// TODO: zu_scene_minify - shrinks all buffers to minimize the memory consumption of the scene
// Also should test whether reallocating to move the buffers closer together affects performance

void zu_scene_draw(struct zu_scene *scene, GLuint fb) {
	//glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb);
	//glClearColor(0,0,0,0);
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glBindVertexArray(scene->vao);

	// TODO: per-material shaders
	glUseProgram(scene->shader);

	for (size_t i = 0; i < scene->n_objects; i++) {
		glEnableVertexAttribArray(0);

		struct zu_obj *obj = scene->objects[i];

		mat44 mvp;
		matmul(mvp, scene->cam, obj->transform);
		glUniformMatrix4fv(scene->uniform.mvp, 1, GL_FALSE, mvp);

		glBindBuffer(GL_ARRAY_BUFFER, obj->vtx_buf);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
		glDrawArrays(GL_TRIANGLES, 0, 3 * obj->n_triangles);

		glDisableVertexAttribArray(0);
	}

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
	obj->n_triangles = 0;
	obj->triangles = NULL;
	obj->vtx_buf = 0;

	return obj;
}

void zu_obj_del(struct zu_obj *obj) {
	struct zu_scene *scene = obj->scene;

	if (obj->triangles) free(obj->triangles);
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

GLfloat *zu_obj_geom(struct zu_obj *obj, size_t n_triangles) {
	if (obj->triangles) free(obj->triangles);

	obj->n_triangles = n_triangles;
	// n_triangles * 3 vertices * 3 components * GLfloat
	return obj->triangles = malloc(obj->n_triangles * 3 * 3 * sizeof *obj->triangles);
}

int zu_obj_upload(struct zu_obj *obj) {
	glBindVertexArray(obj->scene->vao);

	if (!obj->vtx_buf) {
		glGenBuffers(1, &obj->vtx_buf);
		if (!obj->vtx_buf) return 1;
	}

	glBindBuffer(GL_ARRAY_BUFFER, obj->vtx_buf);
	glBufferData(GL_ARRAY_BUFFER, obj->n_triangles * 3 * 3 * sizeof *obj->triangles, obj->triangles, GL_STATIC_DRAW);

	return 0;
}
