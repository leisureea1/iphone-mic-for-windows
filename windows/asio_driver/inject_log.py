import re

with open('src/iphone_asio_driver.cpp', 'r', encoding='utf-8') as f:
    code = f.read()

log_header = '#include <fstream>\nvoid asio_log(const char* msg) {\n    std::ofstream out(R"(C:\\Users\\Leisureea\\AppData\\Local\\Temp\\asio_log.txt)", std::ios_base::app);\n    out << msg << "\\n";\n}\n'

if 'asio_log' not in code:
    code = code.replace('namespace iphone_mic {', log_header + 'namespace iphone_mic {\n')

    methods = ['init', 'getDriverName', 'start', 'getChannels', 'getLatencies', 'getBufferSize', 'getSampleRate', 'setSampleRate', 'canSampleRate', 'getChannelInfo', 'createBuffers', 'outputReady']

    for m in methods:
        pattern = r'(ASIO(?:Bool|Error|void)\s+iPhoneAsioDriver::' + m + r'\s*\(.*?\)\s*\{)'
        rep = r'\g<1>\n    ::asio_log("' + m + '");'
        code = re.sub(pattern, rep, code, count=1, flags=re.DOTALL)

    with open('src/iphone_asio_driver.cpp', 'w', encoding='utf-8') as f:
        f.write(code)
