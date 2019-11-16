require 'jimson'

c = Jimson::Client.new("http://localhost:8100/")
p c.start("rtmp://localhost/live/9001","rtmp://localhost/live/9002")
