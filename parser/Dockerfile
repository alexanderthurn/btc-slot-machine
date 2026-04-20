FROM alpine:3.18
RUN apk add --no-cache g++ libstdc++ tini sqlite-dev
WORKDIR /app
COPY main.cpp xr.cpp ./
RUN g++ -std=c++17 -O3 main.cpp -o main -lsqlite3
RUN mkdir -p blocks chunks filter state
ENTRYPOINT ["/sbin/tini", "--", "./main"]
CMD ["parse"]
