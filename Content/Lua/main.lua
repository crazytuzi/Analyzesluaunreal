-- 判断是否有成功引入slua_profiler模块
if slua_profile then
    -- 根据实际的情况填入对应的 host 和 port
    slua_profile.start("127.0.0.1", 8081)
    print("start slua_profiler")
end
xx = {}
function xx.text(v)
    local SluaTestCase = import("SluaTestCase")
    local t = SluaTestCase()
    local TestCount = 1000000
    local start = os.clock()
    for i = 1, TestCount do
        t:EmptyFunc(t)
    end
    print("1m call EmptyFunc, take time", os.clock() - start)
end
function xx.dowhile(a, b, c)
    print("hhhh", a, b, c)
    return 4, 5
end

function xx.dotest()
end

do
    print("hello world")
    return 1024
end
