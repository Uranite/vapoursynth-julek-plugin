vapoursynth-julek-plugin is a collection of some new filters and some already known ones that I am implementing here to have more performance.

Please visit the [wiki](https://github.com/dnjulek/vapoursynth-julek-plugin/wiki) for more details.

### Building:

```
Requirements:
    - Git
    - C++17 compiler
    - CMake >= 3.23
```
### Linux:
```bash
git clone --recurse-submodules https://github.com/dnjulek/vapoursynth-julek-plugin

cd vapoursynth-julek-plugin/thirdparty

mkdir libjxl_build
cd libjxl_build

cmake -C ../libjxl_cache.cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -G Ninja ../libjxl

cmake --build .
cmake --install .

cd ../..

cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -G Ninja

cmake --build build
sudo cmake --install build
```

I recommend compiling with clang or you may have problems with libjxl, if you want to try compiling with gcc you may need to add this to the second cmake command:\
``-DCMAKE_C_FLAGS=fPIC -DCMAKE_CXX_FLAGS=-fPIC``
### Windows:
For some reason, the plugin fails to load if compiled with MSVC, use Clang instead. Select Clang in your installation of C++ Build Tools, or download Clang from [LLVM](https://github.com/llvm/llvm-project/releases) as the one from C++ Build Tools may be outdated. Only MSVC and the Windows SDK should be required in your installation of C++ Build Tools.

Open the ``Visual Studio 2022 Developer PowerShell`` and use cd to the folder you downloaded the repository.
```pwsh
# Recommended, uncomment to use:
# $env:CFLAGS = "-march=native -flto -ffast-math"
# $env:CXXFLAGS = "-march=native -flto -ffast-math"

git clone --recurse-submodules https://github.com/dnjulek/vapoursynth-julek-plugin

cd vapoursynth-julek-plugin/thirdparty

cmake --fresh -C ./libjxl_cache.cmake -G Ninja ./libjxl -B libjxl_build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang

cmake --build libjxl_build

cmake --install libjxl_build

cd ..

# You may need to adjust the location of VS_INCLUDE_DIR
cmake --fresh -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DVS_INCLUDE_DIR="C:/Program Files/VapourSynth/sdk/include/vapoursynth" -DCMAKE_CXX_COMPILER=clang

cmake --build build
```
