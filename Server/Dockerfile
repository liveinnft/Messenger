FROM ubuntu:22.04

# Установка зависимостей
RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

# Создание рабочей директории
WORKDIR /app

# Копирование исходников
COPY . .

# Компиляция
RUN mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc)

# Открываем порт
EXPOSE 8888

# Запуск сервера
CMD ["./build/Server"]
