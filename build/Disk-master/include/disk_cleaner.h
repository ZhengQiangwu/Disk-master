// disk_cleaner.h
#ifndef DISK_CLEANER_H
#define DISK_CLEANER_H

#include <cstdint>

#ifdef _WIN32
    #ifdef DISK_CLEANER_EXPORTS
        #define API __declspec(dllexport)
    #else
        #define API __declspec(dllimport)
    #endif
#else
    #define API __attribute__((visibility("default")))
#endif

/**
 * @brief 文件分类位掩码。
 * 可以使用 | (位或) 操作符进行组合。
 * 例如: CATEGORY_VIDEO | CATEGORY_AUDIO
 */
enum FileCategory {
    CATEGORY_UNKNOWN     = 0,

    // --- 通过文件扫描发现的项 ---
    // --- 可清理项 ---
    CATEGORY_TRASH       = 1 << 0,  // 1	回收站
    CATEGORY_PACKAGES    = 1 << 1,  // 2	安装包
    CATEGORY_COMPRESSED  = 1 << 2,  // 4	压缩包
    
    // --- 可搬迁项 ---
    CATEGORY_VIDEO       = 1 << 3,  // 8	视频文件
    CATEGORY_AUDIO       = 1 << 4,  // 16	音频文件
    CATEGORY_IMAGE       = 1 << 5,  // 32	图片文件
    CATEGORY_DOCUMENT    = 1 << 6,  // 64

    // --- 通过直接路径访问的特殊清理项 ---
    CATEGORY_THUMBNAIL_CACHE = 1 << 7,  // 128 (代表.cache下的thumbnails明确、独立的项)----图片缩略图缓存
    CATEGORY_OTHER_APP_CACHE = 1 << 8,  // 256 (代表.cache下除thumbnails外的所有内容)----用户应用缓存

    // --- 便捷组合 (更新) ---
    CATEGORY_ALL_CLEANUP = CATEGORY_TRASH | CATEGORY_PACKAGES | CATEGORY_COMPRESSED | CATEGORY_THUMBNAIL_CACHE | CATEGORY_OTHER_APP_CACHE,	//清理全部
    CATEGORY_ALL_MIGRATE = CATEGORY_VIDEO | CATEGORY_AUDIO | CATEGORY_IMAGE | CATEGORY_DOCUMENT	//搬迁全部
};

struct FileInfo {
    char* path;	//文件路径
    uint64_t size;	//文件大小(字节数)
    FileCategory category;//文件类别
};

/**
 * @brief 扫描进度回调函数类型定义
 * 
 * @param file_path 扫描到的文件路径
 * @param file_size 文件大小 (Bytes)
 * @param total_scanned_size 当前已扫描到的垃圾文件总大小 (Bytes)
 * @param category 文件所属分类
 */
typedef void (*ScanCallback)(const char* file_path, uint64_t file_size, uint64_t total_scanned_size, FileCategory category);

extern "C" {

/**
 * @brief 启动异步磁盘扫描
 * 
 * @param home_path 要扫描的用户主目录路径 (例如 "/home/user")
 * @param callback 回调函数，用于在扫描过程中实时返回文件信息
 */
API void StartScan(const char* home_path, ScanCallback callback);

/**
 * @brief 请求停止正在进行的后台扫描。
 * 这是一个异步请求，扫描不会立即停止，但会在下一次检查时退出。
 */
API void StopScan();

/**
 * @brief 检查扫描是否已完成
 * 
 * @return int 1 表示完成，0 表示正在进行中
 */
API int IsScanFinished();

/**
 * @brief 获取扫描结果
 * 
 * @param category 要获取的文件分类
 * @param count [out] 用于接收文件数量的指针
 * @return FileInfo* 返回一个 FileInfo 数组，使用后需要调用 free_scan_results 释放
 */
API FileInfo* GetScanResults(FileCategory category, int* count);

/**
 * @brief 释放由 get_scan_results 分配的内存
 * 
 * @param results get_scan_results 返回的数组指针
 * @param count 数组中的元素数量
 */
API void FreeScanResults(FileInfo* results, int count);

/**
 * @brief 将指定文件列表移动到目标目录
 * 
 * @param file_paths 要移动的文件路径数组
 * @param count 文件数量
 * @param destination_dir 目标目录路径
 * @return int 0 表示成功，-1 表示失败
 */
API int MoveFiles(const char** file_paths, int count, const char* destination_dir);

/**
 * @brief 为指定的文件类别设置自定义的文件扩展名列表。
 *        这会覆盖默认设置。只对基于扩展名的类别有效。
 * @param category 要修改的单个 FileCategory 枚举值。
 * @param extensions 一个C风格的字符串数组，包含新的扩展名 (例如, [".log", ".tmp"])。
 * @param count 数组中的扩展名数量。
 */
API void SetExtensions(FileCategory category, const char* extensions[], int count);

/**
 * @brief 获取特殊类别垃圾的大小（这些不是通过全盘扫描得到的）。
 * 
 * @param category 单个 FileCategory 枚举值 (例如 CATEGORY_USER_APP_CACHE)。
 * @return uint64_t 返回该类别占用的总字节数。
 */
API uint64_t GetSpecialCategorySize(FileCategory category);

/**
 * @brief 根据提供的位掩码清理一个或多个文件/垃圾类别。
 *        这是所有清理操作的统一入口。
 * @param category_mask 使用 | 组合的 FileCategory 枚举值。
 * @return uint64_t 返回实际清理的总字节数。
 */
API uint64_t CleanupCategories(unsigned int category_mask);

/**
 * @brief 清理指定文件夹下的所有可删除文件。
 * 
 * @param dir_path 要清理的文件夹的绝对路径。
 * @return uint64_t 返回清理的总字节数。
 */
API uint64_t CleanupDirectory(const char* dir_path);

/**
 * @brief 根据提供的位掩码搬迁一个或多个文件类别。
 * 
 * @param category_mask 使用 | 组合的 FileCategory 枚举值。
 * @param destination_dir 目标目录的绝对路径。
 * @return int 0 表示成功，-1 表示失败。
 */
API int MigrateCategories(unsigned int category_mask, const char* destination_dir);

/**
 * @brief 清理扫描器资源，等待后台线程结束。必须在程序退出前调用。
 */
API void CleanupScanner();
} // extern "C"

#endif // DISK_CLEANER_H