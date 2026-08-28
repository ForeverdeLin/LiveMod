#ifndef BEAUTYFILTER_H
#define BEAUTYFILTER_H
                                                    //已修改完成，可在ai记录里找YUV直接处理美颜方案
#include <stdint.h>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>

/**
 * @brief 美颜滤镜参数
 */
struct BeautyFilterParams {
    float smoothLevel;      // 磨皮强度 (0.0-1.0)
    float brightLevel;      // 亮度调整 (-1.0-1.0)
    float contrastLevel;    // 对比度调整 (-1.0-1.0)
    float saturationLevel; // 饱和度调整 (-1.0-1.0)
    bool enableFaceDetect; // 是否启用人脸检测（用于局部美颜）
};

/**
 * @brief OpenCV美颜滤镜模块
 * 实现实时美颜效果：磨皮、美白、亮度/对比度调整
 * 支持全图美颜和基于人脸检测的局部美颜
 */
class BeautyFilter
{
public:
    BeautyFilter();
    ~BeautyFilter();

    /**
     * @brief 初始化美颜滤镜
     * @param width 图像宽度
     * @param height 图像高度
     * @return 成功返回true
     */
    bool Initialize(int width, int height);

    /**
     * @brief 应用美颜滤镜
     * @param input YUV420P格式输入数据
     * @param inputSize 输入数据大小
     * @param output 输出YUV420P数据缓冲区
     * @param outputSize 输出缓冲区大小
     * @param params 美颜参数
     * @return 成功返回true
     */
    bool ApplyFilter(uint8_t* input, int inputSize, uint8_t* output, 
                     int* outputSize, const BeautyFilterParams& params);

    /**
     * @brief 释放资源
     */
    void Uninitialize();

    /**
     * @brief 设置默认美颜参数
     */
    static BeautyFilterParams GetDefaultParams();

private:
    /**
     * @brief YUV420P转BGR
     * cv::Mat 是 OpenCV 的图像数据结构：
    * 存储图像像素数据
    * 包含宽度、高度、通道数、数据类型等信息
    类似一个二维数组，但更强大
     */
    void YUV420PToBGR(uint8_t* yuvData, int width, int height, cv::Mat& bgr);

    /**
     * @brief BGR转YUV420P
     */
    void BGRToYUV420P(const cv::Mat& bgr, uint8_t* yuvData, int* outputSize);

    /**
     * @brief 应用磨皮效果（双边滤波）
     */
    void ApplySmooth(cv::Mat& image, float level);

    /**
     * @brief 调整亮度
     */
    void AdjustBrightness(cv::Mat& image, float level);

    /**
     * @brief 调整对比度
     */
    void AdjustContrast(cv::Mat& image, float level);

    /**
     * @brief 调整饱和度
     */
    void AdjustSaturation(cv::Mat& image, float level);

    /**
     * @brief 综合美颜处理
     */
    void ApplyBeautyEffects(cv::Mat& image, const BeautyFilterParams& params);

private:
    bool m_bInitialized;
    int m_width;
    int m_height;
    
    cv::Mat m_tempBGR;          // 临时BGR图像
    cv::Mat m_tempYUV;          // 临时YUV图像
    
    // 人脸检测相关（可选，如果需要局部美颜）
    cv::CascadeClassifier m_faceCascade;
    bool m_bFaceDetectEnabled;
};

#endif // BEAUTYFILTER_H

