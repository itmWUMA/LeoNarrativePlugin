# golden: 全命令覆盖的正例，必须零诊断编译通过
label lab_start
bg bg_school_room transition=fade duration=1.5
bgm bgm_daily01 volume=0.8 fade=2
se se_door_open volume=0.5
char 李雷 char_lico_smile at=left pose=normal motion=in
voice v0101_0001
text 李雷 | 你好，"世界"！
text - | 旁白没有说话人。
text 直接整行都是正文也可以。
wait 800
set x = 1
set y = 2 + 3 * 4
set x += 10
set y -= x
set y *= 2
set y /= 3
setg affection = 0
setg affection += 1
jumpif x == 1 && (y >= 3 || !false) -> lab_far
jump lab_start

label lab_far
set s = "a\"b\\c"
set ok = s == "abc" || 1 < 2 && 3.5 >= 3
set neg = -5
set f = -0.5 + 1.25

choice
    去图书馆 -> lab_a
    回家 -> lab_b
    追上去 -> lab_c if affection >= 5 && x != 0

label lab_a
jump lab_end
label lab_b
jump lab_end
label lab_c
jump lab_end

label lab_end
bgm -
char 李雷 -
text - | 完
end
