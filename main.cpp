#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>
#include <sys/shm.h>
#include <random>

#define EMPTY 0
#define FULL 1
#define MUTEX 2
#define GET 3
#define SET 4

#define MAX_SIZE 15

union semun {
    int val;                /* value for SETVAL */
    struct semid_ds *buf;   /* buffer for IPC_STAT & IPC_SET */
    ushort *array;          /* array for GETALL & SETALL */
    struct seminfo *__buf;  /* buffer for IPC_INFO */
};

int semid;
union semun tmp;
struct sembuf sb;
int shared_mem_id;

struct Buffer {
    int arr[MAX_SIZE]{};
    int elementCount = 0;
    int head = 0;
    int tail = 0;
};

void decrement(unsigned short num) {
    sb.sem_num = num;
    sb.sem_op = -1;
    semop(semid, &sb, 1);
}

void increment(unsigned short num) {
    sb.sem_num = num;
    sb.sem_op = 1;
    semop(semid, &sb, 1);
}

static int rand_int(int min, int max) {    //C++11 way of generating "trully" random values
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<int> dist(min, max);
    return dist(mt);
}

int main() {
    int i = 0;
    int n = 0;
    pid_t pid;

    semid = semget(IPC_PRIVATE, 5, 0666 | IPC_CREAT); // 5elementowa tablica semaforow, z read-write permission dla wszystkich (no execute) rw-rw-rw-
    shared_mem_id = shmget(IPC_PRIVATE, sizeof(Buffer), 0777 | IPC_CREAT); // read-write permission dla wszystkich (with execute) rwxrwxrwx

    auto *buff = (Buffer *) shmat(shared_mem_id, nullptr, 0); // dolacza segment pamieci dzielonej o id shared_mem_id do przestrzeni adresowej procesu (i pozniej jego dzieci)

    tmp.val = MAX_SIZE;
    semctl(semid, EMPTY, SETVAL, tmp); // 'empty' semaphore

    tmp.val = 0;
    semctl(semid, FULL, SETVAL, tmp); //  'full' semaphore

    tmp.val = 1;
    semctl(semid, MUTEX, SETVAL, tmp); //  mutex

    tmp.val = 1;
    semctl(semid, GET, SETVAL, tmp); //  mutex

    tmp.val = 1;
    semctl(semid, SET, SETVAL, tmp); //  mutex

    for (i = 0; i < 2; ++i) // tworzymy procesy konsumentow i prodycentow
        pid = fork();

    if (pid < 0)
        perror("fork fail");
    else if (pid == 0) {    // proces produceta (child)
        std::cout << "child PID: " << getpid() << std::endl;
        while (true) {
            usleep(static_cast<__useconds_t>(rand_int(200, 300))); // jakotakie zroznicowanie czasowe "checi" dostepu do bufora
            n = rand_int(1, 4); // randomowa ilosc elementow do dodania do bufora

            decrement(SET); // tylko 1 pisarz
            std::cout << "   prod: " << getpid() << " dodaje (" << n << ")" << std::endl;

            for (i = 0; i < n; i++) {   // dodajemy elementy
                decrement(EMPTY);   // zmniejszenie o 1 semafora o rozmiarze bufora (jesli 0 (czyli pelny bufor) to sie zawiesza i koljeny prod nie moze wejsc w sekcje (mutex SET))
                decrement(MUTEX);   // mutex zapewniajacy atomowe dodanie

                buff->arr[buff->head] = getpid();
                buff->elementCount++;
                buff->head = (buff->head + 1) % MAX_SIZE;

                increment(MUTEX);
                increment(FULL); // ile elementow
            }
            if (buff->elementCount == MAX_SIZE) {
                std::cout << "   pelny bufor, prod czeka" << std::endl;
            }
            std::cout << "\tBuffer:";
            for (i = buff->tail; i < buff->head; i++) {
                std::cout << " " << buff->arr[i] << " ";
            }
            std::cout << std::endl;
            increment(SET);

            if (rand_int(1, 90) == 27)
                break;
        }
        return 0;
    } else if (pid > 0) {   // proces konsumenta (parent)
        std::cout << "parent PID: " << getpid() << std::endl;
        while (true) {
            usleep(static_cast<__useconds_t>(rand_int(200, 300)));

            n = rand_int(1, 4);

            decrement(GET);
            std::cout << "   kons: " << getpid() << " czyta (" << n << ")" << std::endl;
            for (i = 0; i < n; ++i) {
                decrement(FULL); // jak 0 to pusty i nie moze czytac , a po inkrementacji w producencie, bedzie mogl juz czytac
                decrement(MUTEX);

                //auto val = buff->arr[buff->tail];
                buff->elementCount--;
                buff->tail = (buff->tail + 1) % MAX_SIZE;

                increment(MUTEX);
                increment(EMPTY);

            }
            if (buff->elementCount == 0) {
                std::cout << "   pusty bufor, kons czeka" << std::endl;

            }
            std::cout << "\tBuffer:";
            for (i = buff->tail; i < buff->head; i++) {
                std::cout << " " << buff->arr[i] << " ";
            }
            std::cout << std::endl;
            increment(GET);

            if (rand_int(1, 100) == 27)
                break;
        }
        return 0;
    }
}