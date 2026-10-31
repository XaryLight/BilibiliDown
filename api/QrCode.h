#ifndef BILI_API_QR_CODE_H
#define BILI_API_QR_CODE_H

#include <QString>
#include <QImage>
#include <cstdint>

namespace bili::api {

/**
 * @brief 二维码生成器 (字节模式, 容错等级 L/M/Q/H, 版本 1-40)
 *
 *  基于 Nayuki qrcodegen 算法 (MIT License) 的紧凑实现:
 *  https://github.com/nayuki/QR-Code-generator
 *
 *  仅使用其中的编码/纠错/掩码选择核心逻辑, 不依赖任何第三方库.
 */
class QrCode {
public:
    enum class Ecc {
        Low = 0,     // 7%
        Medium = 1,  // 15%
        Quartile = 2,// 25%
        High = 3     // 30%
    };

    // 生成二维码, 失败返回空 QImage (数据过长等)
    static QImage generate(const QString &text, Ecc ecc = Ecc::Medium,
                           int minVersion = 1, int maxVersion = 40,
                           int scale = 8, int border = 4);

    // 查询可容纳 dataLen 字节所需的最小版本 (-1 表示超出 maxVersion)
    static int minVersionFor(int dataLen, Ecc ecc, int maxVersion = 40);

private:
    // Reed-Solomon GF(256) 初始化
    static void ensureTables();
    static uint8_t gfMul(uint8_t a, uint8_t b);
    static void rsComputeRemainder(const uint8_t *data, int dataLen,
                                   const uint8_t *generator, int genLen,
                                   uint8_t *remainder);

    // 编码 + 绘制 (一次完成: 数据->纠错->矩阵->掩码)
    static bool encodeAndDraw(const QByteArray &data, int version, Ecc ecc,
                              uint8_t *modules, int &size);
};

} // namespace bili::api

#endif // BILI_API_QR_CODE_H
