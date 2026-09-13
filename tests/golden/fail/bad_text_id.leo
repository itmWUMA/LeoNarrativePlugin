# 显式文本 ID 含斜杠但字符非法 → E_BAD_TEXT_ID（无斜杠的 id=xxx 视为正文，不报错）
text - | 你好。 id=x/a!b
end
