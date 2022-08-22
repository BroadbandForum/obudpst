FROM alpine as build-env
RUN apk add build-base && apk add cmake
WORKDIR /app

COPY . .
COPY ./tests/ci-test.sh .
# Compile the binaries
RUN cmake . && make

FROM alpine
RUN apk add iproute2-tc
COPY --from=build-env /app/udpst /app/udpst
COPY --from=build-env /app/ci-test.sh /app/ci-test.sh
RUN chmod +x /app/ci-test.sh
WORKDIR /app
ENTRYPOINT ["/app/ci-test.sh"]
