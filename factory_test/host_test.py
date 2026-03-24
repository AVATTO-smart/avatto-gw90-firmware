#!/usr/bin/env python3
"""
AVATTO GW90-TI 产测上位机脚本
===============================

功能:
  - 通过串口与产测固件通信
  - 解析测试结果 JSON
  - 生成测试报告 (CSV / 屏幕输出)
  - 支持批量测试

使用:
  python host_test.py --port COM3
  python host_test.py --port /dev/ttyUSB0 --output report.csv

依赖:
  pip install pyserial
"""

import serial
import json
import time
import argparse
import csv
import sys
import os
from datetime import datetime


# ANSI 颜色
class Color:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    CYAN = '\033[96m'
    BOLD = '\033[1m'
    END = '\033[0m'


def colorize(text, color):
    """给文本添加颜色(仅终端)"""
    if sys.stdout.isatty():
        return f"{color}{text}{Color.END}"
    return text


class FactoryTester:
    def __init__(self, port, baud=115200, timeout=60):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.ser = None
        self.results = []
        self.summary = None
        self.raw_log = []

    def connect(self):
        """连接串口"""
        print(f"\n连接串口 {self.port} @ {self.baud}...")
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=1)
            time.sleep(0.5)
            # 清空缓冲区
            self.ser.reset_input_buffer()
            print(colorize("  串口连接成功", Color.GREEN))
            return True
        except serial.SerialException as e:
            print(colorize(f"  串口连接失败: {e}", Color.RED))
            return False

    def disconnect(self):
        """断开串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_command(self, cmd):
        """发送命令"""
        self.ser.write(f"{cmd}\n".encode())
        self.ser.flush()

    def run_test(self):
        """执行测试流程"""
        self.results = []
        self.summary = None
        self.raw_log = []

        print(colorize("\n" + "=" * 60, Color.CYAN))
        print(colorize("  AVATTO GW90-TI 产线测试", Color.BOLD))
        print(colorize("=" * 60, Color.CYAN))
        print(f"  时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"  端口: {self.port}")
        print()

        # 发送启动命令
        self.send_command("CMD:START")

        # 读取结果
        start_time = time.time()
        btn_prompted = False

        while time.time() - start_time < self.timeout:
            if self.ser.in_waiting > 0:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if not line:
                    continue

                self.raw_log.append(line)

                # 解析 RSP: 行
                if line.startswith("RSP:"):
                    json_str = line[4:]
                    try:
                        data = json.loads(json_str)
                    except json.JSONDecodeError:
                        continue

                    # 按键提示
                    if data.get("action") == "PRESS_BTN_NOW":
                        if not btn_prompted:
                            print(colorize("\n  >>> 请按下设备按键 <<<\n", Color.YELLOW))
                            btn_prompted = True
                        continue

                    # 测试结果
                    if "test" in data and "result" in data:
                        self.results.append(data)
                        passed = data["result"] == "PASS"
                        status = colorize("PASS", Color.GREEN) if passed else colorize("FAIL", Color.RED)
                        print(f"  [{status}] {data['test']:20s} | {data.get('detail', '')}")

                    # 汇总结果
                    if data.get("type") == "SUMMARY":
                        self.summary = data
                        break

                # 普通日志行
                elif line.startswith("["):
                    # 进度信息
                    print(f"  {colorize(line, Color.CYAN)}")

        # 打印汇总
        if self.summary:
            self._print_summary()
        else:
            print(colorize("\n  超时: 未收到测试汇总!", Color.RED))

        return self.summary

    def _print_summary(self):
        """打印测试汇总"""
        s = self.summary
        verdict = s.get("verdict", "UNKNOWN")
        total = s.get("total", 0)
        passed = s.get("pass", 0)
        failed = s.get("fail", 0)
        failures = s.get("failures", [])

        print()
        print(colorize("=" * 60, Color.CYAN))
        if verdict == "PASS":
            print(colorize(f"  ★★★ 测试通过 ★★★  ({passed}/{total})", Color.GREEN + Color.BOLD))
        else:
            print(colorize(f"  ✖✖✖ 测试失败 ✖✖✖  ({passed}/{total})", Color.RED + Color.BOLD))
            print(colorize(f"  失败项: {', '.join(failures)}", Color.RED))
        print(colorize("=" * 60, Color.CYAN))
        print()

    def get_mac(self):
        """从结果中提取 MAC 地址"""
        for r in self.results:
            if r.get("test") == "mac_addr" and r.get("result") == "PASS":
                return r.get("detail", "UNKNOWN")
        return "UNKNOWN"

    def save_report(self, filepath):
        """保存报告到 CSV"""
        file_exists = os.path.exists(filepath)

        with open(filepath, 'a', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)

            # 写表头
            if not file_exists:
                headers = ["时间", "MAC", "总判定"]
                headers += [r["test"] for r in self.results]
                headers += ["失败项"]
                writer.writerow(headers)

            # 写数据行
            row = [
                datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                self.get_mac(),
                self.summary.get("verdict", "UNKNOWN") if self.summary else "TIMEOUT"
            ]
            for r in self.results:
                row.append(f"{r['result']}: {r.get('detail', '')}")

            failures = self.summary.get("failures", []) if self.summary else ["TIMEOUT"]
            row.append(", ".join(failures) if failures else "NONE")
            writer.writerow(row)

        print(f"  报告已保存: {filepath}")

    def save_raw_log(self, filepath):
        """保存原始串口日志"""
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(f"# AVATTO GW90 Factory Test Log\n")
            f.write(f"# Time: {datetime.now().isoformat()}\n")
            f.write(f"# Port: {self.port}\n\n")
            for line in self.raw_log:
                f.write(line + "\n")


def main():
    parser = argparse.ArgumentParser(description='AVATTO GW90-TI 产测上位机')
    parser.add_argument('--port', required=True, help='串口端口 (COM3 or /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=115200, help='波特率 (默认 115200)')
    parser.add_argument('--output', default='factory_test_report.csv', help='CSV 报告输出文件')
    parser.add_argument('--log-dir', default='logs', help='原始日志保存目录')
    parser.add_argument('--batch', action='store_true', help='批量模式 (测试完一个自动等下一个)')
    parser.add_argument('--timeout', type=int, default=60, help='单次测试超时 (秒)')
    args = parser.parse_args()

    tester = FactoryTester(args.port, args.baud, args.timeout)

    if not tester.connect():
        sys.exit(1)

    # 创建日志目录
    os.makedirs(args.log_dir, exist_ok=True)

    try:
        test_count = 0
        while True:
            test_count += 1
            print(colorize(f"\n{'='*60}", Color.BOLD))
            print(colorize(f"  第 {test_count} 台设备测试", Color.BOLD))
            print(colorize(f"{'='*60}", Color.BOLD))

            summary = tester.run_test()

            # 保存报告
            tester.save_report(args.output)

            # 保存原始日志
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            mac = tester.get_mac().replace(":", "")
            log_file = os.path.join(args.log_dir, f"test_{timestamp}_{mac}.log")
            tester.save_raw_log(log_file)

            if not args.batch:
                break

            # 批量模式: 等待操作员放下一块板
            print(colorize("\n  等待下一台设备...", Color.YELLOW))
            print("  (按回车开始下一台, 输入 q 退出)")

            user_input = input("  > ").strip().lower()
            if user_input == 'q':
                break

            # 重新连接(设备可能重新上电)
            tester.disconnect()
            time.sleep(2)
            if not tester.connect():
                print(colorize("  串口重连失败, 退出", Color.RED))
                break

    except KeyboardInterrupt:
        print(colorize("\n\n  用户中断测试", Color.YELLOW))
    finally:
        tester.disconnect()
        print(f"\n  共测试 {test_count} 台设备")
        print(f"  报告: {args.output}")
        print()


if __name__ == '__main__':
    main()
