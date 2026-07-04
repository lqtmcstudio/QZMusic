package love.qz.apkpatcher

/**
 * APK 增量更新 Patcher
 *
 * 用法：
 * ```kotlin
 * val oldApk = context.applicationInfo.sourceDir
 * val result = Patcher.applyPatch(oldApk, patchPath, newApkPath)
 * if (result == 0) { /* 成功 */ }
 * ```
 *
 * patch 文件由 gen.exe 在服务端生成：
 * `gen.exe <oldApk> <newApk> <output.patch>`
 */
object Patcher {

    init {
        System.loadLibrary("apkpatch")
    }

    /**
     * 将 patch 应用到 APK 文件，生成新 APK。
     *
     * @param oldApkPath  当前已安装的 APK 路径
     * @param patchPath   gen.exe 生成的 patch 文件路径
     * @param newApkPath  新 APK 的输出路径
     * @return 0 表示成功，-1 表示失败
     */
    @JvmStatic
    external fun applyPatch(oldApkPath: String, patchPath: String, newApkPath: String): Int
}
