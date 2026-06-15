import os

os.makedirs('my_traces', exist_ok=True)

test_cases = {
    "test01_zero_space.rep": "a 0 0\na 1 16\nr 1 0\nf 0\nf 1",
    "test02_align_ghost.rep": "a 0 1\na 1 1\na 2 1\nf 1\na 3 2\nf 0\nf 2\nf 3",
    "test03_alt_free.rep": "a 0 128\na 1 128\na 2 128\na 3 128\nf 0\nf 2\nf 1\nf 3",
    "test04_heap_push.rep": "a 0 40960\na 1 32\nf 0\na 2 40960\nf 1\nf 2",
    "test05_immediate_reuse.rep": "a 0 256\nf 0\na 1 256\nf 1\na 2 256\nf 2",
    "test06_swiss_cheese.rep": "a 0 64\na 1 64\na 2 64\na 3 64\na 4 64\nf 0\nf 2\nf 4",
    "test07_forward_coal.rep": "a 0 64\na 1 64\na 2 64\nf 0\nf 1\nf 2",
    "test08_backward_coal.rep": "a 0 64\na 1 64\na 2 64\nf 2\nf 1\nf 0",
    "test09_perfect_split.rep": "a 0 1024\nf 0\na 1 32\na 2 32\na 3 32\na 4 32",
    "test10_trinity_coal.rep": "a 0 128\na 1 128\na 2 128\nf 0\nf 2\nf 1",
    "test11_tiny_burst.rep": "a 0 32\nf 0\na 1 32\nf 1\na 2 32\nf 2\na 3 32\nf 3",
    "test12_waterfall.rep": "a 0 32\na 1 64\na 2 128\na 3 256\na 4 512\nf 0\nf 2\nf 1\nf 4\nf 3",
    "test13_reverse_waterfall.rep": "a 0 512\na 1 256\na 2 128\na 3 64\nf 0\nf 1\nf 2\nf 3",
    "test14_oscillation.rep": "a 0 32\na 1 2048\nf 0\na 2 32\nf 1\na 3 2048\nf 2\nf 3",
    "test15_weird_sizes.rep": "a 0 17\na 1 33\na 2 99\na 3 1023\nf 1\nf 3\nf 0\nf 2",
    "test16_inplace_realloc.rep": "a 0 64\nr 0 64\nr 0 64\nr 0 64\nf 0",
    "test17_micro_shrink.rep": "a 0 128\nr 0 100\nf 0",
    "test18_massive_shrink.rep": "a 0 2048\nr 0 32\nf 0",
    "test19_forced_move.rep": "a 0 32\na 1 32\na 2 32\nr 1 4096\nf 0\nf 2\nf 1",
    "test20_yoyo_realloc.rep": "a 0 50\nr 0 16\nr 0 80\nr 0 20\nr 0 100\nf 0"
}

for filename, commands in test_cases.items():
    lines = commands.strip().split('\n')
    num_ops = len(lines)
    
    # 修复后的正确头部顺序
    header = f"20000\n10\n{num_ops}\n1\n"
    
    with open(f"my_traces/{filename}", "w") as f:
        f.write(header + commands.strip() + "\n")

print(f"成功！已在 my_traces/ 目录下修复并重新生成了 {len(test_cases)} 个 .rep 测试文件。")
