#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import argparse
import sys
import os
from pathlib import Path
from cmark import CMark

def compare_files(actual, expected):
    """比较两个HTML内容是否相同"""
    # 移除Windows和Unix换行符的差异
    return actual == expected

def read_file(file_path):
    """读取文件内容"""
    with open(file_path, 'r', encoding='utf-8') as f:
        return f.read()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Test markdown files conversion.')
    parser.add_argument('--program', dest='program', nargs='?', default=None,
            help='program to test')
    parser.add_argument('--library-dir', dest='library_dir', nargs='?',
            default=None, help='directory containing dynamic library')
    parser.add_argument('--test-dir', dest='test_dir', default='test/plugins_test',
            help='directory containing test files')
    
    args = parser.parse_args(sys.argv[1:])

    # 初始化 CMark
    cmark = CMark(prog=args.program, library_dir=args.library_dir, extensions='math mathblock')

    # 统计数据
    passed = 0
    failed = 0
    errored = 0

    # 获取所有测试文件
    test_dir = Path(args.test_dir)
    md_files = list(test_dir.glob('*.md'))

    print(f"Found {len(md_files)} test files in {test_dir}")
    print("\nTesting markdown files:")

    for md_file in md_files:
        html_file = md_file.with_suffix('.html')
        
        if not html_file.exists():
            print(f"Warning: No matching HTML file for {md_file}")
            continue

        try:
            # 读取 markdown 文件
            md_content = read_file(md_file)
            expected_html = read_file(html_file)

            # 转换为 HTML
            [rc, actual_html, err] = cmark.to_html(md_content)

            if rc != 0:
                errored += 1
                print(f"{md_file.name} [ERRORED (return code {rc})]")
                print(err)
            elif compare_files(actual_html, expected_html):
                passed += 1
                print(f"{md_file.name} [PASSED]")
            else:
                failed += 1
                print(f"{md_file.name} [FAILED]")
                print("Expected:")
                print(repr(expected_html))
                print("Got:")
                print(repr(actual_html))

        except Exception as e:
            errored += 1
            print(f"{md_file.name} [ERRORED (exception)]")
            print(str(e))

    # 打印总结
    print(f"\nTest Summary:")
    print(f"{passed} passed, {failed} failed, {errored} errored")

    # 设置退出码
    if failed == 0 and errored == 0:
        sys.exit(0)
    else:
        sys.exit(1)
