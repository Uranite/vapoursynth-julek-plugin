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

cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -G Ninja

cmake --build build
sudo cmake --install build
```

I recommend compiling with clang or you may have problems with libjxl, if you want to try compiling with gcc you may need to add this to the second cmake command:\
``-DCMAKE_C_FLAGS=fPIC -DCMAKE_CXX_FLAGS=-fPIC``
### Windows:
Clang is recommended for faster performance. Download Clang from [LLVM](https://github.com/llvm/llvm-project/releases) as the one from Visual Studio may be outdated. Only MSVC and the Windows SDK should be required in your installation of Visual Studio.

Open ``Developer PowerShell for VS`` and use cd to the folder you downloaded the repository.
```pwsh
git clone --recurse-submodules https://github.com/dnjulek/vapoursynth-julek-plugin
cd vapoursynth-julek-plugin

cmake --fresh -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_FLAGS="-march=native -flto" -DCMAKE_C_FLAGS="-march=native -flto"

ninja -C build
```
