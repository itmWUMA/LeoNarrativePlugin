# 本地化显式文本 ID（spec §8）：送翻前 freeze 写入，键稳定不受插行影响
text - | 开场自动 ID（章节/_root/0）。
text 李雷 | 你好，世界。 id=demo/intro/hello
text 纯旁白也行。 id=demo/intro/narr
text - | 我的 ID 是 id=admin
choice
    去图书馆 -> branch_library id=demo/intro/c0/0
    直接回家 -> branch_home if flag >= 1 id=demo/intro/c0/1
label branch_library
text - | 到了图书馆。
end
label branch_home
text - | 回家了。
end
