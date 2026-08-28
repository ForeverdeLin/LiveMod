/**
 * @file BeautyFilter.cpp
 * @brief 美颜滤镜实现
 * @details 使用OpenCV实现视频美颜效果（磨皮、增亮、对比度调整等）
 *          知识点：OpenCV图像处理、YUV格式转换、图像滤波算法
 */

#include "BeautyFilter.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>

/**
 * @brief 构造函数
 * @details 初始化美颜滤镜对象
 */
BeautyFilter::BeautyFilter()
    : m_bInitialized(false)        // 初始化标志：未初始化
    , m_width(0)                   // 视频宽度
    , m_height(0)                  // 视频高度
    , m_bFaceDetectEnabled(false)  // 人脸检测标志：未启用
{
}

BeautyFilter::~BeautyFilter()
{
    Uninitialize();
}

/**
 * @brief 获取默认美颜参数
 * @details 返回默认的美颜效果参数
 * @return 默认参数结构
 */
BeautyFilterParams BeautyFilter::GetDefaultParams()
{
    BeautyFilterParams params;
    params.smoothLevel = 0.5f;      // 中等磨皮（0.0-1.0，值越大磨皮越强）
    params.brightLevel = 0.1f;      // 轻微增亮（0.0-1.0，值越大越亮）
    params.contrastLevel = 0.1f;    // 轻微增强对比度（0.0-1.0）
    params.saturationLevel = 0.0f;  // 不调整饱和度（0.0-1.0）
    params.enableFaceDetect = false; // 默认不启用人脸检测
    return params;
}

/**
 * @brief 初始化美颜滤镜
 * @details 知识点：OpenCV初始化、Haar级联分类器
 *          1. 预分配临时缓冲区（BGR和YUV格式）
 *          2. 加载人脸检测分类器（可选）
 * @param width 视频宽度（像素）
 * @param height 视频高度（像素）
 * @return true=成功，false=失败
 */
bool BeautyFilter::Initialize(int width, int height)
{
    if (m_bInitialized) {
        printf("BeautyFilter already initialized\n");
        return false;
    }

    m_width = width;
    m_height = height;

    // 知识点：预分配临时缓冲区
    // cv::Mat - OpenCV的矩阵类，用于存储图像数据
    // CV_8UC3: 8位无符号整数，3通道（BGR格式）
    // CV_8UC1: 8位无符号整数，1通道（YUV格式）
    m_tempBGR = cv::Mat(height, width, CV_8UC3);      // BGR格式缓冲区
    m_tempYUV = cv::Mat(height * 3 / 2, width, CV_8UC1); // YUV420P格式缓冲区

    m_bInitialized = true;
    printf("BeautyFilter initialized: %dx%d\n", width, height);

    return true;
}

/**
 * @brief 应用美颜滤镜
 * @details 知识点：图像处理流程
 *          1. YUV420P转BGR（OpenCV处理需要BGR格式）
 *          2. 应用美颜效果（磨皮、增亮、对比度等）
 *          3. BGR转YUV420P（输出回YUV格式）
 * @param input 输入的YUV420P数据
 * @param inputSize 输入数据大小（字节）
 * @param output 输出的YUV420P数据缓冲区
 * @param outputSize 输出参数，输出数据大小（字节）
 * @param params 美颜参数
 * @return true=成功，false=失败
 */
bool BeautyFilter::ApplyFilter(uint8_t* input, int inputSize, uint8_t* output,
                               int* outputSize, const BeautyFilterParams& params)
{
    if (!m_bInitialized || !input || !output || !outputSize) {
        return false;
    }

    // 检查输入数据大小
    // YUV420P格式大小：width * height * 3 / 2
    // Y平面：width * height
    // U平面：width * height / 4
    // V平面：width * height / 4
    int expectedSize = m_width * m_height * 3 / 2;
    if (inputSize < expectedSize) {//输入的数据不够，宽高是初始化的时候设置的
        //确保输入的数据长度足够存储一帧 YUV420p 图像。
        printf("Input data size too small: %d < %d\n", inputSize, expectedSize);
        return false;
    }

    // 知识点：YUV420P转BGR
    // OpenCV的图像处理函数需要BGR格式，所以先转换
    YUV420PToBGR(input, m_width, m_height, m_tempBGR);

    // 知识点：应用美颜效果
    // 在BGR格式上应用各种图像处理算法
    ApplyBeautyEffects(m_tempBGR, params);

    // 知识点：BGR转YUV420P
    // 处理完成后转换回YUV格式（用于编码）
    BGRToYUV420P(m_tempBGR, output, outputSize);

    return true;
}

/**
 * @brief YUV420P格式转BGR格式
 * @details 实现步骤：
 *          1. 从YUV420P数据中提取Y、U、V三个平面（Y为完整分辨率，UV为1/4分辨率）
 *          2. 将U和V平面上采样到与Y相同的尺寸（使用”线性插值“）
 *          3. 合并YUV三个平面为完整的YUV图像
 *          4. 使用OpenCV的cvtColor函数将YUV转换为BGR格式；颜色空间转换函数
 *          
 * 
 * 线性插值是一种平滑的缩放方法，通过周围像素的加权平均计算新像素值，使缩放后的图像过渡自然
 * 
 *          知识点：YUV420P格式结构
 *          YUV420P（平面格式）：
 *          - Y平面：width * height（完整分辨率）
 *          - U平面：width/2 * height/2（1/4分辨率）
 *          - V平面：width/2 * height/2（1/4分辨率）
 * @param yuvData YUV420P数据
 * @param width 图像宽度
 * @param height 图像高度
 * @param bgr 输出的BGR Mat对象
 */
void BeautyFilter::YUV420PToBGR(uint8_t* yuvData, int width, int height, cv::Mat& bgr)
{
    // 知识点：YUV420P格式布局
    // 数据排列：Y平面 + U平面 + V平面（平面格式，非交错）
    int ySize = width * height;      // Y平面大小
    int uvSize = ySize / 4;           // U或V平面大小（各为Y的1/4）

    uint8_t* yPlane = yuvData;                    // Y平面起始位置
    uint8_t* uPlane = yuvData + ySize;            // U平面起始位置
    uint8_t* vPlane = yuvData + ySize + uvSize;  // V平面起始位置


    cv::Mat yMat(height, width, CV_8UC1, yPlane);
    cv::Mat uMat(height / 2, width / 2, CV_8UC1, uPlane);
    cv::Mat vMat(height / 2, width / 2, CV_8UC1, vPlane);

    // 上采样UV到Y的尺寸
    cv::Mat uMatUpsampled, vMatUpsampled;
    cv::resize(uMat, uMatUpsampled, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);//INTER_LINEAR线性插值
    cv::resize(vMat, vMatUpsampled, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);

    // 合并YUV
    std::vector<cv::Mat> yuvPlanes = {yMat, uMatUpsampled, vMatUpsampled};
    cv::Mat yuvMat;
    cv::merge(yuvPlanes, yuvMat);

    // YUV转BGR
    cv::cvtColor(yuvMat, bgr, cv::COLOR_YUV2BGR);
}

/**
 * @brief BGR格式转YUV420P格式
 * @details 实现步骤：
 *          1. 使用OpenCV的cvtColor函数将BGR转换为YUV格式
 *          2. 分离YUV三个平面（Y、U、V各一个平面）
 *          3. 直接复制Y平面到输出缓冲区（完整分辨率）
 *          4. 对U和V平面进行下采样（缩小到1/4分辨率，使用线性插值）
 *          5. 将下采样后的U和V平面复制到输出缓冲区（Y平面之后）
 *          
 *          输出格式：Y平面（完整分辨率）+ U平面（1/4分辨率）+ V平面（1/4分辨率）
 * @param bgr 输入的BGR Mat对象
 * @param yuvData 输出的YUV420P数据缓冲区
 * @param outputSize 输出参数，输出数据大小（字节）
 */
void BeautyFilter::BGRToYUV420P(const cv::Mat& bgr, uint8_t* yuvData, int* outputSize)
{
    if (bgr.empty() || !yuvData || !outputSize) {
        return;
    }

    int width = bgr.cols;
    int height = bgr.rows;
    int ySize = width * height;
    int uvSize = ySize / 4;
    *outputSize = ySize + uvSize * 2;

    // BGR转YUV
    cv::Mat yuvMat;
    cv::cvtColor(bgr, yuvMat, cv::COLOR_BGR2YUV);

    // 分离YUV平面
    std::vector<cv::Mat> yuvPlanes;
    cv::split(yuvMat, yuvPlanes);

    // 复制Y平面
    memcpy(yuvData, yuvPlanes[0].data, ySize);

    // 下采样UV平面
    cv::Mat uDownsampled, vDownsampled;
    cv::resize(yuvPlanes[1], uDownsampled, cv::Size(width / 2, height / 2), 
               0, 0, cv::INTER_LINEAR);
    cv::resize(yuvPlanes[2], vDownsampled, cv::Size(width / 2, height / 2), 
               0, 0, cv::INTER_LINEAR);

    // 复制U和V平面
    memcpy(yuvData + ySize, uDownsampled.data, uvSize);
    memcpy(yuvData + ySize + uvSize, vDownsampled.data, uvSize);
}

/**
 * @brief 应用磨皮效果（双边滤波）
 * @details 实现步骤：
 *          1. 使用双边滤波（bilateralFilter）对图像进行平滑处理，保留边缘细节，两个现成函数
 *          2. 根据磨皮强度（level）动态调整滤波参数（颜色空间和坐标空间的标准差*比例）
 *          3. 将原图和磨皮后的图像按比例混合（alpha = level），实现可调节的磨皮强度
 *          
 *          知识点：双边滤波
 *          - 同时考虑空间距离和颜色相似度，既能平滑皮肤又能保留边缘
 *          - sigmaColor：颜色空间标准差，控制颜色差异的容忍度
 *          - sigmaSpace：坐标空间标准差，控制空间距离的影响范围
 * 
 * 
 * sigmaColor：颜色相似度的容忍度（值越大，更多不同颜色的像素参与）
sigmaSpace：空间距离的影响范围（值越大，更远的像素也有影响）
 * @param image 输入的BGR图像（会被修改）
 * @param level 磨皮强度（0.0-1.0，值越大磨皮效果越强）
 */
void BeautyFilter::ApplySmooth(cv::Mat& image, float level)//最终有用的是image
{
    if (level <= 0.0f) {
        return;
    }

    // 使用双边滤波实现磨皮效果
    // 参数：d（邻域直径）、sigmaColor（颜色空间标准差）、sigmaSpace（坐标空间标准差）
    int d = 9;// 滤波时考虑的周围像素范围大小；考虑周围 9×9 的区域（81个像素）
    double sigmaColor = 75.0 * level;  // 颜色空间滤波强度，颜色差异在 75 以内的像素会被考虑
    //颜色相似度的容忍度，控制哪些像素会被用于平滑
    double sigmaSpace = 75.0 * level;  // 空间滤波强度；空间距离的影响范围，控制距离越远的像素影响越小

    cv::Mat smoothed;//这是存储纯磨皮的图像
    cv::bilateralFilter(image, smoothed, d, sigmaColor, sigmaSpace);///主要用到库函数实现
    //OpenCV 提供的双边滤波函数不需要自己实现算法，直接调用即可

    // 按比例混合原图和磨皮后的图像；完全使用磨皮图会丢失细节；混合可以保留部分原图信息

    float alpha = level;//这里原图image和纯磨皮smoothed混合后生成结果图像覆盖在image里！

    cv::addWeighted(image, 1.0f - alpha, smoothed, alpha, 0, image);
}





/**代码中使用的图像格式是 CV_8UC3（BGR）和 CV_8UC1（灰度），所以RGB各自和加权后的范围都是0-255
 * @brief 调整图像亮度
 * @details 使用OpenCV的convertTo方法，通过调整像素值的偏移量（delta参数）实现亮度调整。
 * 计算公式：new_pixel = pixel（原图像中某个像素的原始值（B、G、R三个通道分别处理）） * 1.0 + delta，
 * 其中delta = level * 50。
 *          level范围-1.0到1.0，正数增亮（像素值增加），负数变暗（像素值减少）。
 * @param image 输入的BGR图像（会被修改）
 * @param level 亮度调整强度（-1.0到1.0，正数增亮，负数变暗）
 */
void BeautyFilter::AdjustBrightness(cv::Mat& image, float level)
{
    if (level == 0.0f) {
        return;
    }

    // 亮度调整：level范围-1.0到1.0
    // 正数增亮，负数变暗
    int delta = (int)(level * 50); // 调整幅度
    image.convertTo(image, -1, 1.0, delta);
}




/**
 * @brief 调整图像对比度；对比度 = 图像中最亮部分与最暗部分的差异程度
 * 缩放系数改变的是：各个像素值之间的相对差异。
 * @details 使用OpenCV的convertTo方法，通过调整像素值的缩放系数（alpha参数）实现对比度调整。
 *          计算公式：new_pixel = pixel * alpha + 0，其中alpha = 1.0 + level。
 *          level范围-1.0到1.0，正数增强对比度（alpha>1，像素值差异放大），负数降低对比度（alpha<1，像素值差异缩小）。
 * @param image 输入的BGR图像（会被修改）
 * @param level 对比度调整强度（-1.0到1.0，正数增强对比度，负数降低对比度）
 */
void BeautyFilter::AdjustContrast(cv::Mat& image, float level)
{
    if (level == 0.0f) {
        return;
    }

    // 对比度调整：level范围-1.0到1.0
    // 正数增强对比度，负数降低对比度
    double alpha = 1.0 + level; // 对比度系数
    image.convertTo(image, -1, alpha, 0);
}




/**
 * @brief 调整图像饱和度；饱和度（Saturation）表示颜色的鲜艳程度：
 *         高饱和度：颜色鲜艳、纯正（如鲜红、鲜蓝）
 *         低饱和度：颜色灰暗、接近黑白（如灰色、淡色）
 *         饱和度为0：完全黑白（灰度图）
 * 
 * 为什么用HSV色彩空间？
BGR格式中，颜色和亮度混合在一起，直接调整会影响亮度和颜色。HSV将图像分解为：
H（Hue，色相）：颜色类型（红、绿、蓝等）
S（Saturation，饱和度）：颜色鲜艳程度
V（Value，亮度）：明暗程度
这样可以直接调整饱和度，而不影响色相和亮度
 * @details 将BGR图像转换为HSV色彩空间，分离出S（饱和度）通道后，使用缩放系数（alpha = 1.0 + level）调整饱和度值。
 *          计算公式：new_saturation = saturation * alpha，其中alpha = 1.0 + level。
 *          level范围-1.0到1.0，正数增强饱和度（颜色更鲜艳），负数降低饱和度（颜色更灰暗，接近黑白）。
 *          调整后使用threshold限制饱和度范围（0-255），最后合并通道并转换回BGR格式。
 * @param image 输入的BGR图像（会被修改）
 * @param level 饱和度调整强度（-1.0到1.0，正数增强饱和度，负数降低饱和度）
 */
void BeautyFilter::AdjustSaturation(cv::Mat& image, float level)
{
    if (level == 0.0f) {
        return;
    }

    // 转换到HSV色彩空间
    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);

    // 分离通道
    // channels[0] = H通道（色相）
    // channels[1] = S通道（饱和度）← 我们要调整的
    // channels[2] = V通道（亮度）
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);

    // 饱和度调整：level范围-1.0到1.0
    double alpha = 1.0 + level;//alpha是*的
    channels[1].convertTo(channels[1], -1, alpha, 0);

    // 限制饱和度范围
    cv::threshold(channels[1], channels[1], 255, 255, cv::THRESH_TRUNC);

    // 合并通道
    cv::merge(channels, hsv);

    // 转换回BGR并给image
    cv::cvtColor(hsv, image, cv::COLOR_HSV2BGR);
}

/**
 * @brief 应用美颜效果到图像
 * @details 按顺序应用多种美颜效果：先磨皮（双边滤波平滑皮肤），再调整颜色属性（亮度、对比度、饱和度），
 *          最后可选地基于人脸检测进行局部增强（在人脸区域应用更强的磨皮效果）。
 *          
 *          顺序原因：
 *          1. 先磨皮：磨皮是基础美颜效果，应在颜色调整之前进行，避免颜色调整影响磨皮的平滑效果。
 *          2. 再调整颜色：亮度、对比度、饱和度是全局颜色调整，在磨皮后的平滑图像上调整效果更自然。
 *          3. 最后局部美颜：人脸检测和局部增强应在全局效果之后，作为可选的精细化处理。
 * @param image 输入的BGR图像（会被修改）
 * @param params 美颜参数结构体，包含各种效果的强度值
 */
void BeautyFilter::ApplyBeautyEffects(cv::Mat& image, const BeautyFilterParams& params)
{
    // 应用各种美颜效果
    // 注意：顺序很重要，通常先磨皮，再调整颜色

    // 1. 磨皮（双边滤波）
    if (params.smoothLevel > 0.0f) {
        ApplySmooth(image, params.smoothLevel);
    }

    // 2. 亮度调整
    if (params.brightLevel != 0.0f) {
        AdjustBrightness(image, params.brightLevel);
    }

    // 3. 对比度调整
    if (params.contrastLevel != 0.0f) {
        AdjustContrast(image, params.contrastLevel);
    }

    // 4. 饱和度调整
    if (params.saturationLevel != 0.0f) {
        AdjustSaturation(image, params.saturationLevel);
    }

}

void BeautyFilter::Uninitialize()
{
    if (!m_bInitialized) {
        return;
    }

    m_tempBGR.release();
    m_tempYUV.release();
    m_faceCascade.~CascadeClassifier();

    m_bInitialized = false;
    printf("BeautyFilter uninitialized\n");
}

