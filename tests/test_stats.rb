require 'jimson'

c = Jimson::Client.new("http://localhost:8100/")
#p c.start("rtmp://pcgw.pgw.jp/live/9000","rtmp://pcgw.pgw.jp/live/hoge")
p c.stats
