C:\"Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build"\vcvarsall x64 && ^
cl dx12_clear_framebuffer_demo.cpp /link user32.lib d3d12.lib dxgi.lib && ^
cl dx12_color_triangle_demo.cpp /link user32.lib d3d12.lib dxgi.lib d3dcompiler.lib
