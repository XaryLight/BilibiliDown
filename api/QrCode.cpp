#include "QrCode.h"
#include "Logger.h"

#include <QPainter>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

namespace bili::api {

// ============================================================
//  Reed-Solomon GF(256)
// ============================================================
static uint8_t s_gfExp[512];
static uint8_t s_gfLog[256];
static bool s_tablesReady = false;

void QrCode::ensureTables() {
    if (s_tablesReady) return;
    s_tablesReady = true;
    uint8_t x = 1;
    for (int i = 0; i < 255; i++) {
        s_gfExp[i] = x;
        s_gfLog[x] = static_cast<uint8_t>(i);
        x <<= 1;
        if (x & 0x100) x ^= 0x11D; // 本原多项式 x^8+x^4+x^3+x^2+1
    }
    for (int i = 255; i < 512; i++)
        s_gfExp[i] = s_gfExp[i - 255];
}

uint8_t QrCode::gfMul(uint8_t a, uint8_t b) {
    ensureTables();
    if (a == 0 || b == 0) return 0;
    return s_gfExp[s_gfLog[a] + s_gfLog[b]];
}

void QrCode::rsComputeRemainder(const uint8_t *data, int dataLen,
                                const uint8_t *generator, int genLen,
                                uint8_t *remainder) {
    ensureTables();
    std::vector<uint8_t> res(genLen, 0);
    for (int i = 0; i < dataLen; i++) {
        uint8_t factor = data[i] ^ res[0];
        for (int j = 0; j < genLen - 1; j++)
            res[j] = res[j + 1];
        res[genLen - 1] = 0;
        for (int j = 0; j < genLen; j++)
            res[j] ^= gfMul(generator[j], factor);
    }
    for (int j = 0; j < genLen; j++)
        remainder[j] = res[j];
}

// ============================================================
//  容量表  [ecl][version] (索引 0 为占位)
// ============================================================
static constexpr int8_t ECC_CODEWORDS_PER_BLOCK[4][41] = {
    {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
};
static constexpr int8_t NUM_ECC_BLOCKS[4][41] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
};

static int getNumRawDataModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7) result -= 36;
    }
    return result;
}

static int getNumDataCodewords(int ver, int ecl) {
    return getNumRawDataModules(ver) / 8
           - ECC_CODEWORDS_PER_BLOCK[ecl][ver] * NUM_ECC_BLOCKS[ecl][ver];
}

int QrCode::minVersionFor(const int dataLen, Ecc ecl, int maxVersion) {
    int e = static_cast<int>(ecl);
    for (int v = 1; v <= maxVersion; v++) {
        int capacityBits = getNumDataCodewords(v, e) * 8;
        int charCountBits = (v <= 9) ? 8 : 16;
        int needed = 4 + charCountBits + dataLen * 8;
        if (needed <= capacityBits) return v;
    }
    return -1;
}

// ============================================================
//  辅助函数
// ============================================================
namespace {

void appendBits(std::vector<uint8_t> &bits, uint32_t val, int n) {
    for (int i = n - 1; i >= 0; i--)
        bits.push_back(static_cast<uint8_t>((val >> i) & 1));
}

std::vector<uint8_t> bitsToBytes(const std::vector<uint8_t> &bits, int totalBytes) {
    std::vector<uint8_t> bytes(totalBytes, 0);
    for (size_t i = 0; i < bits.size() && i / 8 < static_cast<size_t>(totalBytes); i++) {
        if (bits[i])
            bytes[i / 8] |= static_cast<uint8_t>(0x80 >> (i % 8));
    }
    return bytes;
}

std::vector<int> alignmentPositions(int ver) {
    if (ver == 1) return {};
    int numAlign = ver / 7 + 2;
    int step = (ver == 32) ? 26 : (ver * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
    std::vector<int> result;
    for (int i = 0, pos = ver * 4 + 10; i < numAlign - 1; i++, pos -= step)
        result.push_back(pos);
    result.push_back(6);
    std::ranges::reverse(result);
    return result;
}

inline void setModule(uint8_t *modules, int size, int x, int y, bool dark) {
    if (x < 0 || y < 0 || x >= size || y >= size) return;
    modules[y * size + x] = dark ? 1 : 0;
}

inline void setFunction(uint8_t *modules, uint8_t *func, int size,
                        int x, int y, bool dark) {
    if (x < 0 || y < 0 || x >= size || y >= size) return;
    modules[y * size + x] = dark ? 1 : 0;
    func[y * size + x] = 1;
}

void drawFinderAt(uint8_t *modules, uint8_t *func, int size, int cx, int cy) {
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int x = cx + dx, y = cy + dy;
            int dist = std::max(std::abs(dx), std::abs(dy));
            bool dark = (dist != 2 && dist != 4);
            setFunction(modules, func, size, x, y, dark);
        }
    }
}

void drawAlignmentAt(uint8_t *modules, uint8_t *func, int size, int cx, int cy) {
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            setFunction(modules, func, size, cx + dx, cy + dy,
                        std::max(std::abs(dx), std::abs(dy)) != 1);
}

void putFormatBits(uint8_t *modules, uint8_t *func, int size, int bits) {
    auto set = [&](int x, int y, bool dark) {
        if (x < 0 || y < 0 || x >= size || y >= size) return;
        modules[y * size + x] = dark ? 1 : 0;
        func[y * size + x] = 1;
    };
    // 环绕左上定位图案
    for (int i = 0; i <= 5; i++) set(i, 8, ((bits >> i) & 1) != 0);
    set(7, 8, ((bits >> 6) & 1) != 0);
    set(8, 8, ((bits >> 7) & 1) != 0);
    set(8, 7, ((bits >> 8) & 1) != 0);
    for (int i = 9; i < 15; i++) set(8, 14 - i, ((bits >> i) & 1) != 0);
    // 第二份
    for (int i = 0; i < 8; i++) set(8, size - 1 - i, ((bits >> i) & 1) != 0);
    for (int i = 8; i < 15; i++) set(size - 15 + i, 8, ((bits >> i) & 1) != 0);
    set(8, size - 8, true); // 暗模块
}

void putVersionBits(uint8_t *modules, uint8_t *func, int size, int bits) {
    auto set = [&](int x, int y, bool dark) {
        if (x < 0 || y < 0 || x >= size || y >= size) return;
        modules[y * size + x] = dark ? 1 : 0;
        func[y * size + x] = 1;
    };
    for (int i = 0; i < 18; i++) {
        bool dark = ((bits >> i) & 1) != 0;
        int a = size - 11 + i % 3;
        int b = i / 3;
        set(a, b, dark);
        set(b, a, dark);
    }
}

int computeFormatBits(int mask, int ecl) {
    int eclBits = 0;
    switch (ecl) {
        case 0: eclBits = 1; break; // Low
        case 1: eclBits = 0; break; // Medium
        case 2: eclBits = 3; break; // Quartile
        case 3: eclBits = 2; break; // High
        default: break;
    }
    int data = (eclBits << 3) | mask;
    int rem = data;
    for (int i = 0; i < 10; i++)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    return ((data << 10) | rem) ^ 0x5412;
}

bool maskBit(int mask, int x, int y) {
    switch (mask) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (y / 2 + x / 3) % 2 == 0;
        case 5: return (x * y) % 2 + (x * y) % 3 == 0;
        case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
        case 7: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
        default: return false;
    }
}

void applyMask(int mask, uint8_t *modules, const uint8_t *func, int size) {
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            if (func[y * size + x]) continue;
            if (maskBit(mask, x, y))
                modules[y * size + x] ^= 1;
        }
}

int penaltyScore(const uint8_t *modules, int size) {
    int score = 0;

    // 规则1: 行/列连续同色
    for (int y = 0; y < size; y++) {
        int runColor = modules[y * size];
        int runLen = 1;
        for (int x = 1; x < size; x++) {
            if (modules[y * size + x] == runColor) {
                runLen++;
            } else {
                if (runLen >= 5) score += 3 + (runLen - 5);
                runColor = modules[y * size + x];
                runLen = 1;
            }
        }
        if (runLen >= 5) score += 3 + (runLen - 5);
    }
    for (int x = 0; x < size; x++) {
        int runColor = modules[x];
        int runLen = 1;
        for (int y = 1; y < size; y++) {
            if (modules[y * size + x] == runColor) {
                runLen++;
            } else {
                if (runLen >= 5) score += 3 + (runLen - 5);
                runColor = modules[y * size + x];
                runLen = 1;
            }
        }
        if (runLen >= 5) score += 3 + (runLen - 5);
    }

    // 规则2: 2x2 同色块
    for (int y = 0; y < size - 1; y++)
        for (int x = 0; x < size - 1; x++) {
            int c = modules[y * size + x];
            if (c == modules[y * size + x + 1] && c == modules[(y + 1) * size + x] &&
                c == modules[(y + 1) * size + x + 1])
                score += 3;
        }

    // 规则3: 类定位图案 1:1:3:1:1 (前后各有4个浅模块)
    static constexpr int pattern[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
    static constexpr int patternR[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
    for (int y = 0; y < size; y++) {
        for (int x = 0; x <= size - 11; x++) {
            bool match = true, matchR = true;
            for (int i = 0; i < 11; i++) {
                if (modules[y * size + x + i] != pattern[i]) match = false;
                if (modules[y * size + x + i] != patternR[i]) matchR = false;
            }
            if (match || matchR) score += 40;
        }
    }
    for (int x = 0; x < size; x++) {
        for (int y = 0; y <= size - 11; y++) {
            bool match = true, matchR = true;
            for (int i = 0; i < 11; i++) {
                if (modules[(y + i) * size + x] != pattern[i]) match = false;
                if (modules[(y + i) * size + x] != patternR[i]) matchR = false;
            }
            if (match || matchR) score += 40;
        }
    }

    // 规则4: 深色比例
    int dark = 0;
    for (int i = 0; i < size * size; i++)
        if (modules[i]) dark++;
    int total = size * size;
    int k = static_cast<int>(std::floor(std::abs(dark * 20 - total * 10) / total));
    score += k * 10;
    return score;
}

} // namespace

// ============================================================
//  主流程
// ============================================================
bool QrCode::encodeAndDraw(const QByteArray &data, int version, Ecc ecc,
                           uint8_t *modules, int &size) {
    ensureTables();
    int ecl = static_cast<int>(ecc);
    int numDataCodewords = getNumDataCodewords(version, ecl);

    // ---- 1. 数据编码 (字节模式 0100) ----
    std::vector<uint8_t> bits;
    appendBits(bits, 0x4, 4);
    appendBits(bits, static_cast<uint32_t>(data.size()), (version <= 9) ? 8 : 16);
    for (char c : data)
        appendBits(bits, static_cast<uint8_t>(c), 8);

    int capacityBits = numDataCodewords * 8;
    if (static_cast<int>(bits.size()) > capacityBits)
        return false;

    appendBits(bits, 0, std::min(4, capacityBits - static_cast<int>(bits.size())));
    while (bits.size() % 8 != 0)
        bits.push_back(0);

    std::vector<uint8_t> dataCodewords = bitsToBytes(bits, numDataCodewords);
    static constexpr uint8_t padBytes[2] = {0xEC, 0x11};
    for (int i = static_cast<int>(bits.size()) / 8, p = 0; i < numDataCodewords; i++, p++)
        dataCodewords[i] = padBytes[p % 2];

    // ---- 2. 纠错分块 + 交织 ----
    int numBlocks = NUM_ECC_BLOCKS[ecl][version];
    int eccPerBlock = ECC_CODEWORDS_PER_BLOCK[ecl][version];
    int rawCodewords = getNumRawDataModules(version) / 8;
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockLen = rawCodewords / numBlocks;

    // RS 生成多项式
    std::vector<uint8_t> gen(eccPerBlock, 0);
    {
        std::vector<uint8_t> roots;
        roots.reserve(eccPerBlock);
for (int i = 0; i < eccPerBlock; i++)
            roots.push_back(s_gfExp[i]);
        gen.resize(eccPerBlock + 1, 0);
        gen[eccPerBlock] = 1;
        for (unsigned char root : roots) {
            for (size_t j = 0; j < gen.size() - 1; j++)
                gen[j] = gfMul(gen[j], root) ^ gen[j + 1];
            gen[gen.size() - 1] = 0;
        }
        gen.pop_back();
    }

    std::vector<std::vector<uint8_t>> blocks(numBlocks);
    {
        int k = 0;
        for (int i = 0; i < numBlocks; i++) {
            int blockDataLen = shortBlockLen - eccPerBlock + (i < numShortBlocks ? 0 : 1);
            std::vector<uint8_t> dat(dataCodewords.begin() + k,
                                     dataCodewords.begin() + k + blockDataLen);
            k += blockDataLen;
            std::vector<uint8_t> ecc(eccPerBlock, 0);
            rsComputeRemainder(dat.data(), blockDataLen, gen.data(), eccPerBlock, ecc.data());
            blocks[i].insert(blocks[i].end(), dat.begin(), dat.end());
            blocks[i].insert(blocks[i].end(), ecc.begin(), ecc.end());
        }
    }

    std::vector<uint8_t> finalCodewords;
    finalCodewords.reserve(rawCodewords);
    for (size_t i = 0; i < static_cast<size_t>(shortBlockLen + 1); i++) {
        for (int j = 0; j < numBlocks; j++) {
            if (i < blocks[j].size())
                finalCodewords.push_back(blocks[j][i]);
        }
    }

    // ---- 3. 绘制矩阵 ----
    size = version * 4 + 17;
    std::vector<uint8_t> func(size * size, 0);
    std::fill_n(modules, size * size, 0);

    // 功能图案
    for (int i = 0; i < size; i++) {
        setFunction(modules, func.data(), size, 6, i, i % 2 == 0);
        setFunction(modules, func.data(), size, i, 6, i % 2 == 0);
    }
    drawFinderAt(modules, func.data(), size, 3, 3);
    drawFinderAt(modules, func.data(), size, size - 4, 3);
    drawFinderAt(modules, func.data(), size, 3, size - 4);

    auto positions = alignmentPositions(version);
    int n = static_cast<int>(positions.size());
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))
                continue;
            drawAlignmentAt(modules, func.data(), size, positions[j], positions[i]);
        }

    // 格式/版本占位 (dark module)
    {
        // 格式信息区域标记为功能位 (稍后 drawFormatBits 填值)
        putFormatBits(modules, func.data(), size, 0);
        if (version >= 7) {
            int rem = version;
            for (int i = 0; i < 12; i++)
                rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
            putVersionBits(modules, func.data(), size, (version << 12) | rem);
        }
        // 暗模块
        modules[(size - 8) * size + 8] = 1;
        func[(size - 8) * size + 8] = 1;
    }

    // ---- 4. 数据码流填充 (zigzag) ----
    {
        int bitIdx = 0;
        auto nextBit = [&]() -> int {
            if (bitIdx >= static_cast<int>(finalCodewords.size()) * 8) return 0;
            int b = (finalCodewords[bitIdx / 8] >> (7 - bitIdx % 8)) & 1;
            bitIdx++;
            return b;
        };
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < size; vert++) {
                for (int j = 0; j < 2; j++) {
                    int x = right - j;
                    bool upward = ((right + 1) & 2) == 0;
                    int y = upward ? size - 1 - vert : vert;
                    if (!func[y * size + x]) {
                        modules[y * size + x] = static_cast<uint8_t>(nextBit());
                    }
                }
            }
        }
    }

    // ---- 5. 掩码选择 ----
    int bestMask = 0;
    int bestPenalty = -1;
    for (int mask = 0; mask < 8; mask++) {
        applyMask(mask, modules, func.data(), size);
        int p = penaltyScore(modules, size);
        if (bestPenalty < 0 || p < bestPenalty) {
            bestPenalty = p;
            bestMask = mask;
        }
        applyMask(mask, modules, func.data(), size); // 异或还原
    }
    applyMask(bestMask, modules, func.data(), size);
    putFormatBits(modules, func.data(), size, computeFormatBits(bestMask, static_cast<int>(ecc)));
    return true;
}

QImage QrCode::generate(const QString &text, Ecc ecc, int minVersion, int maxVersion,
                        int scale, int border) {
    QByteArray data = text.toUtf8();
    int version = minVersionFor(data.size(), ecc, maxVersion);
    if (version < 0) {
        Logger::warn("QR: 数据过长, 无法生成二维码");
        return {};
    }
    if (version < minVersion) version = minVersion;

    std::vector<uint8_t> modules(177 * 177, 0); // 最大版本 40 的尺寸
    int size = 0;
    if (!encodeAndDraw(data, version, ecc, modules.data(), size)) {
        Logger::warn("QR: 编码失败");
        return {};
    }

    int imgSize = (size + border * 2) * scale;
    QImage img(imgSize, imgSize, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter painter(&img);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            if (modules[y * size + x])
                painter.drawRect((x + border) * scale, (y + border) * scale, scale, scale);
        }
    painter.end();
    return img;
}

} // namespace bili::api
