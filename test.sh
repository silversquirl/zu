#!/bin/sh
python3 setup.py build_ext -ig && lldb blender -- zu.blend
