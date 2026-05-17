# 🖥️ Запуск сервера Messenger

3 простых способа запустить сервер - выбирайте любой!

## ⚡ Способ 1: WSL (для Windows) - САМЫЙ ПРОСТОЙ

### Время: 5 минут

### Шаг 1: Установите WSL

Откройте **PowerShell от администратора** и выполните:

```powershell
wsl --install
```

Перезагрузите компьютер.

### Шаг 2: Настройте Ubuntu

После перезагрузки откроется Ubuntu:
- Введите username (например: `user`)
- Введите пароль

### Шаг 3: Установите зависимости

```bash
sudo apt update
sudo apt install -y cmake g++ build-essential
```

### Шаг 4: Скопируйте файлы

```bash
# Создайте папку
mkdir -p ~/messenger
cd ~/messenger

# Скопируйте Server из Windows
# Ваш диск C: доступен как /mnt/c/
# Ваш диск D: доступен как /mnt/d/
cp -r /mnt/d/путь/к/проекту/Server/* .

# Или используйте git clone если проект в GitHub
# git clone https://github.com/ваш-username/messenger.git
```

### Шаг 5: Запустите install.sh

```bash
chmod +x install.sh
./install.sh
```

### ✅ Готово!

Сервер запущен на `127.0.0.1:8888`

Теперь в клиенте подключайтесь к `127.0.0.1:8888`

### Полезные команды:

```bash
# Проверить что сервер работает
ps aux | grep Server

# Остановить сервер
pkill -f Server

# Посмотреть лог
tail -f ~/messenger/server.log

# Перезапустить
cd ~/messenger
./install.sh
```

---

## ☁️ Способ 2: VPS/Облачный сервер

### Время: 3 минуты

Подойдёт любой Linux сервер в облаке.

### Шаг 1: Подключитесь к серверу

```bash
ssh root@ваш-ip-адрес
```

### Шаг 2: Скопируйте файлы

**Вариант A:** Через SCP с вашего компьютера:

```bash
# На вашем Windows (PowerShell/CMD)
scp -r Server root@ваш-ip:/root/messenger
```

**Вариант B:** Через Git:

```bash
# На сервере
git clone https://github.com/ваш-username/messenger.git
cd messenger/Server
```

### Шаг 3: Запустите

```bash
chmod +x install.sh
./install.sh
```

### Шаг 4: Откройте порт в firewall

```bash
# Ubuntu/Debian
sudo ufw allow 8888
sudo ufw enable

# CentOS/RHEL
sudo firewall-cmd --add-port=8888/tcp --permanent
sudo firewall-cmd --reload
```

### ✅ Готово!

Теперь подключайтесь к `ваш-ip:8888` из клиента!

---

## 🐳 Способ 3: Docker

### Время: 2 минуты

Самый изолированный способ.

### Шаг 1: Установите Docker

**Ubuntu/Debian:**
```bash
curl -fsSL https://get.docker.com -o get-docker.sh
sh get-docker.sh
```

**Windows:**
- Скачайте Docker Desktop: https://www.docker.com/products/docker-desktop

### Шаг 2: Соберите образ

```bash
cd Server
docker build -t messenger-server .
```

### Шаг 3: Запустите контейнер

```bash
docker run -d -p 8888:8888 --name messenger messenger-server
```

### ✅ Готово!

Сервер работает на порту 8888.

### Полезные команды Docker:

```bash
# Посмотреть логи
docker logs messenger

# Остановить
docker stop messenger

# Запустить снова
docker start messenger

# Удалить контейнер
docker rm -f messenger

# Пересобрать после изменений
docker build -t messenger-server . --no-cache
docker run -d -p 8888:8888 --name messenger messenger-server
```

---

## 🔧 Ручная компиляция (если install.sh не работает)

### Шаг 1: Установите зависимости

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install -y cmake g++ build-essential
```

**CentOS/RHEL:**
```bash
sudo yum install -y cmake gcc-c++ make
```

### Шаг 2: Скомпилируйте

```bash
cd Server
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### Шаг 3: Запустите

```bash
./Server
```

Или в фоне:
```bash
nohup ./Server > ../server.log 2>&1 &
```

---

## 📊 Мониторинг сервера

### Проверка работы

```bash
# Проверить запущен ли сервер
ps aux | grep Server

# Посмотреть порты
netstat -tlnp | grep 8888
# или
ss -tlnp | grep 8888

# Проверить подключения
lsof -i :8888
```

### Просмотр логов

```bash
# Последние 50 строк
tail -50 server.log

# Следить в реальном времени
tail -f server.log

# Искать ошибки
grep ERROR server.log
```

### Перезапуск сервера

```bash
# Остановить старый
pkill -f Server

# Запустить новый
cd ~/messenger/build
nohup ./Server > ../server.log 2>&1 &
```

---

## 🛡️ Безопасность (для продакшена)

### 1. Создайте отдельного пользователя

```bash
sudo useradd -m -s /bin/bash messenger
sudo -u messenger bash
cd ~
# Скопируйте файлы и запускайте от этого пользователя
```

### 2. Настройте firewall

```bash
# Разрешите только нужные IP
sudo ufw allow from 192.168.1.0/24 to any port 8888
```

### 3. Используйте systemd для автозапуска

Создайте `/etc/systemd/system/messenger.service`:

```ini
[Unit]
Description=Messenger Server
After=network.target

[Service]
Type=simple
User=messenger
WorkingDirectory=/home/messenger/build
ExecStart=/home/messenger/build/Server
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Активируйте:
```bash
sudo systemctl enable messenger
sudo systemctl start messenger
sudo systemctl status messenger
```

### 4. Настройте SSL (опционально)

Используйте nginx как reverse proxy с Let's Encrypt сертификатом.

---

## 🐛 Решение проблем

### "Address already in use"

Порт 8888 уже занят:

```bash
# Найдите процесс
sudo lsof -i :8888

# Остановите его
kill PID_процесса

# Или запустите на другом порту
./Server --port 9999  # Если поддерживается
```

### Сервер падает сразу после запуска

```bash
# Посмотрите лог
cat server.log

# Проверьте права
ls -la ./Server
chmod +x ./Server

# Запустите в foreground для отладки
./Server
```

### Клиент не может подключиться

1. **Проверьте что сервер работает:**
   ```bash
   ps aux | grep Server
   ```

2. **Проверьте firewall:**
   ```bash
   sudo ufw status
   sudo ufw allow 8888
   ```

3. **Проверьте IP адрес:**
   ```bash
   ip addr show
   # или
   hostname -I
   ```

4. **Проверьте порт:**
   ```bash
   netstat -tlnp | grep 8888
   ```

### Компиляция не удается

```bash
# Установите все зависимости
sudo apt install -y cmake g++ build-essential

# Очистите и пересоберите
rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make VERBOSE=1
```

---

## 📝 Конфигурация

### Изменить порт

Отредактируйте `server.cpp`, найдите:

```cpp
int port = 8888;  // Измените на нужный
```

Пересоберите:
```bash
cd build
make
```

### Изменить путь к БД

В `database.cpp`:

```cpp
std::string dbPath = "messenger.db";  // Измените путь
```

---

## ✅ Проверка работы

### Тест подключения

```bash
# С того же сервера
telnet localhost 8888

# С другого компьютера
telnet ваш-ip 8888
```

Если подключение установилось - всё работает!

### Тест через curl

```bash
# Отправить тестовое сообщение
echo "REGISTER|testuser|testpass" | nc localhost 8888
```

---

## 🚀 Оптимизация

### Компиляция с оптимизацией

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Увеличение лимитов

В `/etc/security/limits.conf`:

```
messenger soft nofile 10000
messenger hard nofile 10000
```

---

## 📞 Поддержка

Проблемы? Создайте Issue на GitHub!

**Следующий шаг:** Запустите клиент - см. `BUILD_CLIENT.md`

---

**Готово! Ваш сервер должен работать!** 🎉
