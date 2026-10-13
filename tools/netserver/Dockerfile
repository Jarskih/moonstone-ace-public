# Moonstone lobby server + relay (ROADMAP 12.19).  Build from this directory:
#   docker build -t moonstone-netserver .
#   docker run -d --name moonstone-netserver --restart unless-stopped -p 6510:6510 -p 6510:6510/udp moonstone-netserver
FROM python:3.12-slim
WORKDIR /app
COPY server.py netproto.py dashboard.py dashboard.html ./
USER nobody
EXPOSE 6510/tcp 6510/udp 8080
CMD ["python3", "-u", "server.py", "--port", "6510", "--http-port", "8080"]
