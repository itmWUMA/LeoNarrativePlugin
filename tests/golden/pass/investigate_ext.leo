# golden: 自定义命令（investigate）经严格 spec 编译通过
label lab_a
investigate scene_office mode=strict
investigate scene_home
jumpif investigation >= 1 -> lab_a
text - | 完
end
