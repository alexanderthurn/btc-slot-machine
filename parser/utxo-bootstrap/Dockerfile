FROM golang:1.25-alpine

RUN apk add --no-cache ca-certificates
RUN go install github.com/in3rsha/bitcoin-utxo-dump@latest

WORKDIR /work
ENTRYPOINT ["/go/bin/bitcoin-utxo-dump"]
