FROM alpine:3.18
RUN apk add --no-cache g++ libstdc++
WORKDIR /app
COPY main.cpp xr.cpp ./
RUN g++ -std=c++17 -O3 main.cpp -o main
RUN mkdir -p blocks chunks filter
ENTRYPOINT ["./main"]
CMD ["parse"]
