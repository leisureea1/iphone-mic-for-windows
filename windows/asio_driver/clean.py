import re
with open('src/iphone_asio_driver.cpp', 'r', encoding='utf-8') as f:
    c = f.read()
c = re.sub(r'#include <fstream>\nvoid asio_log.*?\}\n', '', c, flags=re.DOTALL)
c = re.sub(r'\s*::asio_log\(\".*?\"\);', '', c)
with open('src/iphone_asio_driver.cpp', 'w', encoding='utf-8') as f:
    f.write(c)
