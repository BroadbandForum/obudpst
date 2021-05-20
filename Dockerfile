FROM alpine as build-env
RUN apk add build-base && apk add cmake
WORKDIR /app
COPY . .
# Compile the binaries
RUN cmake . && make

FROM alpine
COPY --from=build-env /app/udpst /app/udpst
WORKDIR /app
CMD ["/app/udpst", "-s", "-v"] 
