#!/bin/bash

# 高性能音视频转码器 - 简化界面 (支持默认值预填充)

# 检查当前目录是否存在EnhancedTranscoder可执行文件
if [ ! -f "./EnhancedTranscoder" ]; then
    zenity --error --text="错误：找不到 EnhancedTranscoder 程序。\n请确保在 build 目录下运行此脚本。" --title="程序未找到"
    exit 1
fi

# 检查zenity是否安装
if ! command -v zenity &> /dev/null; then
    echo "错误：Zenity未安装。请运行以下命令安装："
    echo "sudo apt install zenity"
    exit 1
fi

# 设置默认值
DEFAULT_INPUT="../media/input.mp4"
DEFAULT_OUTPUT="../media/output.avi"
DEFAULT_SPEED="1.0"
DEFAULT_ROTATION="0"
DEFAULT_BRIGHTNESS="1.0"
DEFAULT_CONTRAST="1.0"

# 主界面：一次性配置所有参数
ALL_PARAMS=$(zenity --forms --title="音视频转码器 - 统一参数配置" \
    --text="🎬 配置所有转码参数（留空使用默认值）：

📁 文件路径：
  输入：$DEFAULT_INPUT
  输出：$DEFAULT_OUTPUT

⚡ 基本参数默认值：
  变速：$DEFAULT_SPEED | 旋转：$DEFAULT_ROTATION°

🎨 滤镜效果：
  模糊、锐化、灰度（选择'是'启用,默认不启用）

📊 图像调节：
  亮度：$DEFAULT_BRIGHTNESS | 对比度：$DEFAULT_CONTRAST" \
    --add-entry="输入文件路径：" \
    --add-entry="输出文件路径：" \
    --add-entry="变速倍数 (0.1-5.0)：" \
    --add-entry="旋转角度 (度)：" \
    --add-combo="启用模糊滤镜：" --combo-values="否|是" \
    --add-combo="启用锐化滤镜：" --combo-values="否|是" \
    --add-combo="启用灰度滤镜：" --combo-values="否|是" \
    --add-entry="亮度 (0.0-2.0)：" \
    --add-entry="对比度 (0.0-2.0)：" \
    --separator="|" \
    --width=600 --height=450)

# 检查用户是否取消
if [ $? -ne 0 ]; then
    zenity --info --text="转码已取消。"
    exit 0
fi

# 解析所有参数
INPUT_FILE=$(echo "$ALL_PARAMS" | cut -d'|' -f1)
OUTPUT_FILE=$(echo "$ALL_PARAMS" | cut -d'|' -f2)
SPEED=$(echo "$ALL_PARAMS" | cut -d'|' -f3)
ROTATION=$(echo "$ALL_PARAMS" | cut -d'|' -f4)
BLUR_CHOICE=$(echo "$ALL_PARAMS" | cut -d'|' -f5)
SHARPEN_CHOICE=$(echo "$ALL_PARAMS" | cut -d'|' -f6)
GRAYSCALE_CHOICE=$(echo "$ALL_PARAMS" | cut -d'|' -f7)
BRIGHTNESS=$(echo "$ALL_PARAMS" | cut -d'|' -f8)
CONTRAST=$(echo "$ALL_PARAMS" | cut -d'|' -f9)

# 使用默认值填充空参数
[ -z "$INPUT_FILE" ] && INPUT_FILE="$DEFAULT_INPUT"
[ -z "$OUTPUT_FILE" ] && OUTPUT_FILE="$DEFAULT_OUTPUT"
[ -z "$SPEED" ] && SPEED="$DEFAULT_SPEED"
[ -z "$ROTATION" ] && ROTATION="$DEFAULT_ROTATION"
[ -z "$BLUR_CHOICE" ] && BLUR_CHOICE="否"
[ -z "$SHARPEN_CHOICE" ] && SHARPEN_CHOICE="否"
[ -z "$GRAYSCALE_CHOICE" ] && GRAYSCALE_CHOICE="否"
[ -z "$BRIGHTNESS" ] && BRIGHTNESS="$DEFAULT_BRIGHTNESS"
[ -z "$CONTRAST" ] && CONTRAST="$DEFAULT_CONTRAST"

# 转换滤镜选择为数值
ENABLE_BLUR=0
ENABLE_SHARPEN=0
ENABLE_GRAYSCALE=0

[ "$BLUR_CHOICE" = "是" ] && ENABLE_BLUR=1
[ "$SHARPEN_CHOICE" = "是" ] && ENABLE_SHARPEN=1
[ "$GRAYSCALE_CHOICE" = "是" ] && ENABLE_GRAYSCALE=1

# 检查输入文件是否存在
if [ ! -f "$INPUT_FILE" ]; then
    zenity --error --text="错误：输入文件不存在：$INPUT_FILE\n请检查文件名和路径。"
    exit 1
fi

# 确认配置并执行
FILTER_STATUS=""
[ "$ENABLE_BLUR" -eq 1 ] && FILTER_STATUS="${FILTER_STATUS}模糊 "
[ "$ENABLE_SHARPEN" -eq 1 ] && FILTER_STATUS="${FILTER_STATUS}锐化 "
[ "$ENABLE_GRAYSCALE" -eq 1 ] && FILTER_STATUS="${FILTER_STATUS}灰度 "
[ -z "$FILTER_STATUS" ] && FILTER_STATUS="无滤镜"

CONFIRM_TEXT="转码配置确认：

📁 输入：$(basename "$INPUT_FILE")
📁 输出：$(basename "$OUTPUT_FILE")
⚡ 变速：${SPEED}x | 旋转：${ROTATION}°
🎨 滤镜：$FILTER_STATUS
📊 亮度：$BRIGHTNESS | 对比度：$CONTRAST

开始转码？"

zenity --question --text="$CONFIRM_TEXT" --title="确认转码" --width=350

if [ $? -ne 0 ]; then
    zenity --info --text="转码已取消。"
    exit 0
fi

# 构造并执行命令
COMMAND="./EnhancedTranscoder \"$INPUT_FILE\" \"$OUTPUT_FILE\" $SPEED $ROTATION $ENABLE_BLUR $ENABLE_SHARPEN $ENABLE_GRAYSCALE $BRIGHTNESS $CONTRAST"

echo "执行命令: $COMMAND" > transcoder_log.txt

# 进度条处理
(
echo "# 正在初始化转码器..."
echo "20"
sleep 1

echo "# 正在处理视频..."
echo "40"

eval $COMMAND >> transcoder_log.txt 2>&1 &
TRANSCODER_PID=$!

while kill -0 $TRANSCODER_PID 2>/dev/null; do
    echo "# 转码进行中..."
    echo "70"
    sleep 2
done

wait $TRANSCODER_PID
TRANSCODER_EXIT_CODE=$?

echo "# 转码完成！"
echo "100"

) | zenity --progress --title="转码进行中" --text="正在处理视频，请稍候..." \
    --percentage=0 --auto-close --auto-kill --width=400


# 结果通知 
OUTPUT_SIZE=""
if [ -f "$OUTPUT_FILE" ]; then
    OUTPUT_SIZE=" ($(du -h "$OUTPUT_FILE" | cut -f1))"
fi

zenity --info --text="🎉 转码完成！\n\n📁 输出文件：$(basename "$OUTPUT_FILE")$OUTPUT_SIZE\n📂 保存位置：$(dirname "$OUTPUT_FILE")\n\n⚡ 处理参数：\n• 变速：${SPEED}x\n• 旋转：${ROTATION}°\n• 滤镜：$FILTER_STATUS\n• 亮度：$BRIGHTNESS | 对比度：$CONTRAST" --title="转码成功" --width=400

# 清理临时文件（如果存在）
# rm -f "$TEMP_FILE"

exit 0
