import os, re

base = r"d:\Mark\0.workspace\Projects\titans-ems-gitee\hc32f460-lcd\docs\HC32F460-guislice实战"

def shift(m):
    n = int(m.group(1))
    return "第%d章" % (n + 25) if 1 <= n <= 15 else m.group(0)

def shift_spaced(m):
    n = int(m.group(1))
    return "第 %d 章" % (n + 25) if 1 <= n <= 15 else m.group(0)

# 1) 重命名章节文件：第1..15章 -> 第26..40章
for fn in os.listdir(base):
    if fn.endswith(".md") and fn.startswith("第"):
        m = re.match(r"^第(\d+)章", fn)
        if m and 1 <= int(m.group(1)) <= 15:
            new = "第%d章" % (int(m.group(1)) + 25) + fn[m.end():]
            os.rename(os.path.join(base, fn), os.path.join(base, new))
            print("renamed:", fn, "->", new)

# 2) 同步替换所有 .md 内的章节引用（含 "第 N 章" 带空格写法）
for fn in os.listdir(base):
    if fn.endswith(".md"):
        p = os.path.join(base, fn)
        with open(p, "r", encoding="utf-8") as f:
            t = f.read()
        t2 = re.sub(r"第(\d+)章", shift, t)
        t2 = re.sub(r"第 (\d+) 章", shift_spaced, t2)
        if t2 != t:
            with open(p, "w", encoding="utf-8") as f:
                f.write(t2)
            print("updated:", fn)

print("RENUMBER DONE")
