# Wordle Server

This project implements a multi-threaded Wordle server in C. The server supports multiple clients simultaneously, allowing them to guess a hidden 5-letter word within a limited number of attempts. The server is designed with proper synchronization to handle concurrent gameplay and supports graceful shutdown.

---

## Features

- **Multi-threaded gameplay**: Each client connection is handled by a separate thread.
- **Valid word checking**: Ensures that guesses are valid Wordle words.
- **Game mechanics**: Provides feedback for each guess (correct letter and position, correct letter but wrong position, or letter not in the word).
- **Statistics tracking**: Maintains counts of total guesses, wins, and losses across all clients.
- **Graceful shutdown**: The server can be terminated using a `SIGUSR1` signal, ensuring all resources are cleaned up.

---

## How to Compile

To compile the server, use the `gcc` compiler:

```bash
gcc -pthread -o wordle_server hw4.c hw4-main.c
```

## How to Run

To start the Wordle server, execute the compiled program with the following arguments:
```bash
./wordle_server <port> <seed> <dictionary_file> <word_count>
```

- **port**: The port number on which the server will listen for client connections.
- **seed**: A seed value for random number generation to ensure reproducibility of hidden word selection.
- **dictionary_file**: The path to a file containing the dictionary of valid Wordle words, each on a new line.
- **word_count**: The number of words to read from the dictionary file.