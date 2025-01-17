### Скачивание файла

В данном задании нужно дополнить реализацию классов `PeerConnect` и `PieceStorage` из предыдущего задания.
Теперь требуется добавить сохранение частей файла на диск, а также сделать так, чтобы `PieceStorage` можно было использовать
из нескольких потоков.
 
Для выполнения данного задания вам понадобится уметь парсить .torrent файлы, запрашивать список пиров, подключаться к ним и скачивать части файла как в предыдущем домашнем задании.
Рекомендуется воспользоваться своим кодом из предыдущего задания.

Файлы, разрешенные для редактирования студентами в рамках данного задания:

- bencode.h / .cpp
- byte_tools.h / .cpp
- message.h / .cpp
- peer.h
- peer_connect.h / .cpp
- piece.h / .cpp
- piece_storage.h / .cpp
- tcp_connect.h / .cpp
- torrent_file.h / .cpp
- torrent_tracker.h / .cpp

Изменения в прочих файлах не будут учитываться при сборке проекта в проверяющей системе.


**Запрещается использовать чужие реализации парсера bencode и .torrent файлов.**


Для сборки проекта потребуется система сборки `CMake`, а также установленные в системе библиотеки `OpenSSL` и `libcurl`.

- Как установить зависимости на Ubuntu 20.04:
```
$ sudo apt-get install cmake libcurl4-openssl-dev libssl-dev
```
- Как установить зависимости на Mac OS через пакетный менеджер `brew` (https://brew.sh/):
```
$ brew install openssl cmake
```


Дополнительная информация:
- Что такое торрент https://ru.wikipedia.org/wiki/BitTorrent
- .torrent файл https://ru.wikipedia.org/wiki/.torrent
- Подробное описание протокола http://www.bittorrent.org/beps/bep_0003.html, https://wiki.theory.org/BitTorrentSpecification
- Формат компактного ответа трекера http://www.bittorrent.org/beps/bep_0023.html