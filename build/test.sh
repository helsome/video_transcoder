#!/bin/bash

# 核心滤镜功能测试脚本
# 专门测试：锐化、灰度、亮度、对比度

echo "======================================="
echo "   核心滤镜功能开关测试"
echo "======================================="

# 基本设置
INPUT_FILE="../media/input.mp4"
OUTPUT_DIR="../media/filter_tests"
mkdir -p "$OUTPUT_DIR"

# 检查程序和文件
if [ ! -f "./EnhancedTranscoder" ]; then
    echo "❌ 找不到程序文件"
    exit 1
fi

if [ ! -f "$INPUT_FILE" ]; then
    echo "❌ 找不到输入文件: $INPUT_FILE"
    exit 1
fi

echo "🎯 专项测试: 锐化、灰度、亮度、对比度"
echo ""

# 测试函数
test_filter() {
    local name="$1"
    local output="$2"
    local params="$3"
    
    echo -n "🧪 测试 $name ... "
    
    ./EnhancedTranscoder "$INPUT_FILE" "$output" $params > /dev/null 2>&1
    
    if [ -f "$output" ] && [ -s "$output" ]; then
        local size=$(du -h "$output" | cut -f1)
        echo "✅ 成功 ($size)"
        return 0
    else
        echo "❌ 失败"
        return 1
    fi
}

# 核心滤镜测试
echo "📋 锐化滤镜测试:"
test_filter "锐化-开启" "$OUTPUT_DIR/sharpen_on.avi"  "1.0 0 0 1 0 1.0 1.0"
echo ""

echo "📋 灰度滤镜测试:"
test_filter "灰度-开启" "$OUTPUT_DIR/grayscale_on.avi"  "1.0 0 0 0 1 1.0 1.0"
echo ""

echo "📋 亮度调节测试:"
test_filter "亮度-增强" "$OUTPUT_DIR/brightness_high.avi"   "1.0 0 0 0 0 1.5 1.0"
test_filter "亮度-降低" "$OUTPUT_DIR/brightness_low.avi"    "1.0 0 0 0 0 0.6 1.0"
echo ""

echo "📋 对比度调节测试:"
test_filter "对比度-增强" "$OUTPUT_DIR/contrast_high.avi"   "1.0 0 0 0 0 1.0 1.6"
test_filter "对比度-降低" "$OUTPUT_DIR/contrast_low.avi"    "1.0 0 0 0 0 1.0 0.7"
echo ""
echo "✨ 核心滤镜测试完成！"