# Dokumentacja Ponga

## Klient

Podział na dwóch graczy: maina i drugiego gracza.

## Serwer

Powinien obsługiwać do **1024** klientów i **512** sesji. Każda gra odbywa się w sesji, która jest poprzedzona przypisaniem do niej graczy z lobby, w którym może być dwóch graczy - **main gry** i drugi gracz. Lobby to sesja gry.

Aktualna pozycja piłki oraz sygnały o stanie rozgrywki są brane od maina gry, aby uprościć aplikację i nie implementować logiki gry w serwerze. Serwer odpowiada tylko za wymianę informacji, ale fizyka gry jest przetwarzana u klientów (u maina gry). 

Co iterację sprawdza czy ostatni komunikat od klienta był później niż 10s temu i jeśli tak to go usuwa z tablicy połączonych (rozłącza go).

## Protokół

### Sposób działania

Komunikacja po **UDP**. Klient i serwer wymieniają się komunikatami o określonej strukturze i maksymalnym rozmiarze **512B** (pakiety o zmiennym rozmiarze). Serwer pamięta adresy klientów w lobby i identyfikuje ich po przydzielanym ID po połączeniu do serwera.

Do weryfikacji poprawności pakietów jest wykorzystywany kod CRC16. Nie ma ponownego wysyłania pakietu, jeśli jest uszkodzony.

### Typy

#### `uint`

- **`uint8`** - unsigned int 8 bit (może reprezentować boolean. Wartość 1 = true, 0 = false)

- **`uint16`** - unsigned int 16 bit

- **`uint32`** - unsigned in 32 bit

#### `float`

Typ float 32 bit pojedyńczej precyzji.

#### `Vector2`

Składa się z:

| Nazwa | Typ    | Opis                                          |
| ----- | ------ | --------------------------------------------- |
| type  | uint32 | Typ wariantu, żeby Godot mógł łatwo dekodować |
| x     | float  | Składowa x                                    |
| y     | float  | Składowa y                                    |

### Struktura

Struktura jest opisana następująco: `[name:size]`, gdzie:

- **name** - nazwa parametru

- **size** - rozmiar pola w bajtach

Każde pole następujące po sobie od lewej do prawej, jest przesunięte w pakiecie o sumaryczną liczbę bajtów pól, które to pole poprzedza.

#### Pakiet

```
[preamble:3][type:1][size:2][data:size][crc:2]
```

| Nazwa | Typ         | Opis                                        |
| ----- | ----------- | ------------------------------------------- |
| type  | uint8       | Typ komunikatu                              |
| size  | uint16      | Rozmiar danych                              |
| data  | uint8[size] | Dane (struktura zależna od typu komunikatu) |
| crc   | uint16      | Kod CRC do sprawdzenia poprawności          |

**Każdy** pakiet może mieć rozmiar od **8B** do **512B**.

### Preambuła

```
0x01 0x02 0x03
```

### Kod do obliczania CRC

```cpp
#include <stdint.h>

uint16_t crc16_mcrf4xx(uint16_t crc, uint8_t *data, size_t len)
{
    if (!data || len < 0)
        return crc;

    while (len--) {
        crc ^= *data++;
        for (int i=0; i<8; i++) {
            if (crc & 1)  crc = (crc >> 1) ^ 0x8408;
            else          crc = (crc >> 1);
        }
    }
    return crc;
}
```

## Typy komunikatów

Każdy komunikaty jest określony następująco: `NUMBER: NAME (DIRECTION)`, gdzie:

- **NUMBER** - typ komunikatu w pakiecie

- **NAME** - nazwa

- **DIRECTION** - kierunek komunikacji (np. Klient -> Serwer lub Serwer -> Klient ale też Main -> Serwer lub Drugi -> Serwer, żeby pokazać którego gracza dotyczy komunikat)

### 0: Połącz (Klient -> Serwer)

Prośba o połączenie do serwera. W wyniku serwer powinien przypisać klientowi ID i je odesłać z powrotem.

Dane są puste.

### 1: Połączono (Serwer -> Klient)

Informacja o poprawnym ustanowieniu połączenia z serwerem.

Dane:

```
[id:2]
```

| Nazwa | Typ      | Opis                  |
| ----- | -------- | --------------------- |
| id    | `uint16` | identyfikator klienta |

### 2: Nie udało się połączyć (Serwer -> Klient)

Informacja o niepoprawnym ustanowieniu połączenia z serwerem.

Dane są puste.

### 3: Rozłącz (Klient -> Serwer)

Prośba o odłączenie od serwera. Klient nie oczekuje na odpowiedź. Jak jest w trakcie gry to wyrzuca go z sesji i kończy sesję.

Dane:

```
[client_id:2]
```

| Nazwa     | Typ      | Opis                  |
| --------- | -------- | --------------------- |
| client_id | `uint16` | Identyfikator klienta |

### 4: Utwórz sesję (Klient -> Serwer)

Prośba o utworzenie sesji. W wyniku serwer powinien utworzyć sesję, przypisać tam klienta i odesłać identyfikator sesji.

Dane:

```
[client_id:2]
```

| Nazwa     | Typ      | Opis                  |
| --------- | -------- | --------------------- |
| client_id | `uint16` | Identyfikator klienta |

### 5: Przypisano do sesji (Serwer -> Klient)

Informacja od serwera o przypisaniu klienta do sesji. Przychodzi też jak ktoś dołączy do sesji.

Dane:

```
[session_id:2][client_id:2][type:1]
```

| Nazwa      | Typ      | Opis                  |
| ---------- | -------- | --------------------- |
| session_id | `uint16` | Identyfikator sesji   |
| client_id  | `uint16` | Identyfikator klienta |
| type       | `uint8`  | Typ klienta           |

#### Typy klienta

- 0 - main

- 1 - secondary

### 6: Nie udało się stworzyć sesji (Serwer -> Klient)

Informacja od serwera o nieudanej próbie utworzenia sesji.

Dane są puste.

### 7: Sesja przestała istnieć (Serwer -> Klient)

Informacja od serwera o zniszczeniu sesji.

Dane:

```
[session_id:2]
```

| Nazwa      | Typ      | Opis                |
| ---------- | -------- | ------------------- |
| session_id | `uint16` | Identyfikator sesji |

### 8: Przyłącz do sesji (Klient -> Serwer)

Prośba o przyłączenie klient do sesji. Jeśli się uda to serwer wyśle komunikat #5.

Dane:

```
[client_id:2][session_id:2]
```

| Nazwa      | Typ      | Opis                                 |
| ---------- | -------- | ------------------------------------ |
| client_id  | `uint16` | Identyfikator klienta do przypisania |
| session_id | `uint16` | Identyfikator sesji                  |

### 9: Nie udało się przypisać do sesji (Serwer -> Klient)

Informacja od serwera jeśli nie uda się przypisać klienta do sesji.

Dane:

```
[session_id:2]
```

| Nazwa      | Typ      | Opis                |
| ---------- | -------- | ------------------- |
| session_id | `uint16` | Identyfikator sesji |

### 10: Odłącz od sesji (Klient -> Serwer)

Prośba o odłączenie klienta od sesji.

Dane:

```
[session_id:2][client_id:2]
```

| Nazwa      | Typ      | Opis                                            |
| ---------- | -------- | ----------------------------------------------- |
| session_id | `uint16` | Identyfikator sesji                             |
| client_id  | `uint16` | Identyfikator klienta, która chce wyjść z sesji |

### 11: Status prośby o wyjście z sesji (Serwer -> Klient)

Status prośby o wyjście z sesji. Jeśli main wyjdzie z sesji i istnieje drugi klient, to on się staje mainem.

Dane:

```
[session_id:2][client_id:2][status:1]
```

| Nazwa      | Typ      | Opis                                              |
| ---------- | -------- | ------------------------------------------------- |
| session_id | `uint16` | Identyfikator sesji                               |
| client_id  | `uint16` | Identyfikator klienta, który chciał wyjść z sesji |
| status     | `uint8`  | Czy mu się udało wyjść z sesji (1 - tak, 0 - nie) |

### 12: Ustaw gotowość do gry (Klient -> Serwer)

Poinformowanie serwera o gotowości klienta do rozpoczęcia gry. Jeśli gra się zakończyła zwycięstwem jednego z graczy, to ten komunikat mówi serwerowi, żeby uruchomił rozgrywkę jeszcze raz od nowa.

Dane:

```
[client_id:2][session_id:2][readiness:1]
```

| Nazwa      | Typ      | Opis                                                      |
| ---------- | -------- | --------------------------------------------------------- |
| client_id  | `uint16` | Identyfikator klienta                                     |
| session_id | `uint16` | Identyfikator sesji                                       |
| readiness  | `uint8`  | 1 jeśli klient jest gotowy, 0 jeśli klient jest niegotowy |

### 13: Gra rozpoczęta (Serwer -> Klient)

Poinformowanie klienta o rozpoczęciu rozgrywki przez serwer.

Dane:

```
[session_id:2]
```

| Nazwa      | Typ      | Opis                |
| ---------- | -------- | ------------------- |
| session_id | `uint16` | Identyfikator sesji |

### 14: Prześlij pozycję piłki (Klient -> Serwer)

Poinformowanie serwera o pozycji piłki przez maina.

Dane:

```
[client_id:2][session_id:2][ball_pos:12][ball_dir:12]
```

| Nazwa      | Typ       | Opis                          |
| ---------- | --------- | ----------------------------- |
| client_id  | `uint16`  | Identyfikator klienta         |
| session_id | `uint16`  | Identyfikator sesji           |
| ball_pos   | `Vector2` | Pozycja piłki                 |
| ball_dir   | `Vector2` | Kierunek poruszania się piłki |

### 15: Poinformuj o pozycji piłki (Serwer -> Klient)

Poinformowanie klienta o pozycji piłki.

Dane:

```
[ball_pos:12][ball_dir:12]
```

| Nazwa    | Typ       | Opis                          |
| -------- | --------- | ----------------------------- |
| ball_pos | `Vector2` | Pozycja piłki                 |
| ball_dir | `Vector2` | Kierunek poruszania się piłki |

### 16: Prześlij pozycję gracza (Klient -> Serwer)

Poinformowanie serwera o pozycji paletki gracza.

Dane:

```
[client_id:2][session_id:2][pos:12][dir:12]
```

| Nazwa      | Typ       | Opis                                                                     |
| ---------- | --------- | ------------------------------------------------------------------------ |
| client_id  | `uint16`  | Identyfikator gracza                                                     |
| session_id | `uint16`  | Identyfikator sesji                                                      |
| pos        | `Vector2` | Pozycja paletki gracza                                                   |
| dir        | `Vector2` | Kierunek poruszania się paletki gracza na potrzeby przewidywania pozycji |

### 17: Poinformuj o pozycji gracza (Serwer -> Klient)

Poinformowanie gracza o pozycji innego gracza.

Dane:

```
[session_id:2][client_id:2][pos:12][dir:12]
```

| Nazwa      | Typ       | Opis                                                       |
| ---------- | --------- | ---------------------------------------------------------- |
| session_id | `uint16`  | Identyfikator sesji                                        |
| client_id  | `uint16`  | Identyfikator gracza, którego dotyczy informacja o pozycji |
| pos        | `Vector2` | Pozycja paletki gracza                                     |
| dir        | `Vector2` | Kierunek poruszania się paletki gracza                     |

### 18: Poinformuj serwer o uzyskaniu punktu (Main -> Serwer)

Poinformowanie serwera o uzyskaniu punktu i konieczności zresetowania pozycji piłki.

Dane:

```
[session_id:2][client_id:2]
```

| Nazwa      | Typ      | Opis                                     |
| ---------- | -------- | ---------------------------------------- |
| session_id | `uint16` | Identyfikator sesji                      |
| client_id  | `uint16` | Identyfikator gracza, który zdobył punkt |

### 19: Poinformuj gracza o uzyskaniu punktu (Serwer -> Drugi)

Poinformowanie drugiego gracza o uzyskaniu punktu.

Dane:

```
[session_id:2][main_score:4][secondary_score:4]
```

| Nazwa           | Typ      | Opis                   |
| --------------- | -------- | ---------------------- |
| session_id      | `uint16` | Identyfikator sesji    |
| main_score      | `uint32` | Punkty maina           |
| secondary_score | `uint32` | Punkty drugiego gracza |

### 20: Poinformuj o wygraniu (Serwer -> Klient)

Poinformowanie klienta o wygranej któregoś z graczy w sesji.

Dane:

```
[session_id:2][client_id:2]
```

| Nazwa      | Typ      | Opis                                   |
| ---------- | -------- | -------------------------------------- |
| session_id | `uint16` | Identyfikator sesji                    |
| client_id  | `uint16` | Identyfikator klienta, który zwyciężył |

### 21: Sygnał, że żyję (Klient -> Serwer)

Poinformowanie serwera o wciąż aktywnym połączeniu z klientem.

Dane są puste.

### 22: Rozłączono (Serwer -> Klient)

Poinformowanie klienta, który nie odpowiada ponad 10s, że został rozłączony.

Dane są puste.

## Podsumowanie

| Klient -> Serwer                         | Serwer -> Klient                         |
|:----------------------------------------:|:----------------------------------------:|
| 0: Połącz                                | 1: Połączono                             |
| 3: Rozłącz                               | 2: Nie udało się połączyć                |
| 4: Utwórz sesję                          | 5: Przypisano do sesji                   |
| 8: Przyłącz do sesji                     | 6: Nie udało się stworzyć sesji          |
| 10: Odłącz od sesji                      | 7: Sesja przestała istnieć               |
| 12: Ustaw gotowość do gry                | 9: Nie udało się przypisać do sesji      |
| 14: Prześlij pozycję piłki               | 11: Status prośby o wyjście z sesji      |
| 16: Prześlij pozycję gracza              | 13: Gra rozpoczęta                       |
| 18: Poinformuj serwer o uzyskaniu punktu | 15: Poinformuj o pozycji piłki           |
| 21: Sygnał, że żyję                      | 17: Poinformuj o pozycji gracza          |
|                                          | 19: Poinformuj gracza o uzyskaniu punktu |
|                                          | 20: Poinformuj o wygraniu                |
