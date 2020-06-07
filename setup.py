from distutils.core import setup, Extension

zu = Extension(
	'ext_zu',
	libraries = ['GL'],
	sources = ['zumodule.c', 'zu.c'])

setup(
	name = "Zu",
	version = "0.1.0",
	description = "Zu for Blender",
	ext_modules = [zu],
)
