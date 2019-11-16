require 'jimson'

fail unless ARGV[0]
c = Jimson::Client.new("http://localhost:8100/")
p c.kill(ARGV[0].to_i)
