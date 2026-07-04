# apkdiff

APK 增量更新模块，基于 [bsdiff](https://github.com/mendsley/bsdiff) + zlib + minizip。

通过对比新旧 APK 中每个文件的内容差异，生成体积远小于完整 APK 的 patch 文件，在设备端应用 patch 生成新 APK。

## 结构

```
├── bsdiff.c / bsdiff.h     # bsdiff 核心算法（原库，未修改）
├── bspatch.c / bspatch.h   # bspatch 核心算法（原库，未修改）
├── common/
│   ├── patch_format.h      # Patch 文件格式定义
│   └── zlib_stream.h/.c    # zlib 压缩/解压，适配 bsdiff/bspatch stream
├── gen/
│   ├── CMakeLists.txt      # 构建 gen.exe
│   └── main.c              # Patch 生成工具入口
├── jni/
│   ├── CMakeLists.txt      # NDK CMake 构建
│   ├── Android.mk          # 备选 ndk-build 构建
│   ├── Application.mk      # NDK 配置
│   ├── apkpatch.c          # JNI native 实现
│   └── Patcher.kt          # Kotlin 包装类
└── third_party/
    └── zlib/               # zlib 源码（作为 submodule 或直接包含）
```

## Patch 文件格式

```
Header:
  [16B]  Magic "APKDIFF/1.0\0\0\0"
  [4B]   uint32_t entry_count

Entry (×entry_count):
  [2B]   uint16_t path_len
  [NB]   path
  [1B]   type  (0=PATCH, 1=NEW, 2=COPY)
  [8B]   int64_t old_size
  [8B]   int64_t new_size
  [4B]   uint32_t compressed_size
  [NB]   zlib compressed data (COPY 时无数据)
```

## gen.exe 编译（Windows）

需要 MSVC + CMake：

```bash
cmake -B gen/build -S gen -G "Visual Studio 18 2026" -A x64
cmake --build gen/build --config Release
```

## gen.exe 用法

```bash
gen.exe <old.apk> <new.apk> <output.patch>
```

## JNI 编译（Android NDK）

```bash
cmake -B jni/build/arm64-v8a -S jni \
  -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=<NDK>/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-21 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_MAKE_PROGRAM=<NDK>/prebuilt/windows-x86_64/bin/make.exe

make -C jni/build/arm64-v8a -j8
```

## Android 端使用

```kotlin
import love.qz.apkpatcher.Patcher

val oldApk = context.applicationInfo.sourceDir
val result = Patcher.applyPatch(oldApk, patchPath, newApkPath)
// result == 0 → 成功
```

## License

BSD 2-Clause (bsdiff/bspatch), zlib License (zlib/minizip)
