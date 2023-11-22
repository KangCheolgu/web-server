#include <stdio.h>
#include "csapp.h"

#define INITIAL_HASHMAP_SIZE 10

typedef struct {
	char *url;        // URi
	char *data;       // data
	size_t size;      // data size
	time_t last_access;
} cache_entry;

//--------------hash-----------------------------
//hashmap
typedef struct {
	cache_entry **table; // hash table
	size_t size;         // size of hash table
} hashmap;

hashmap cache_map;

unsigned long djb2_hash(char *str);
unsigned long sdbm_hash(char *str);
unsigned int double_hashing(char *str, unsigned int table_size, unsigned int collision_cnt);
void hashmap_init(hashmap *hash_map, size_t size);
void hashmap_insert(hashmap *map, cache_entry *entry);
void hashmap_lru(hashmap *map);
cache_entry *hashmap_search(hashmap *map, char *url);
void hashmap_delete(hashmap *map, char *url);

//--------------------hashing func----------------
//Hash func 1
unsigned long djb2_hash(char *str) {
	unsigned long hash = 5381;
	int c;

	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; // hash * 33 + c

	return hash;
}

//Hash func 2
unsigned long sdbm_hash(char *str) {
	unsigned long hash = 0;
	int c;

	while ((c = *str++))
		hash = c + (hash << 6) + (hash << 16) - hash;

	return hash;
}

//double hashing
unsigned int double_hashing(char *str, unsigned int table_size, unsigned int collision_cnt) {
	unsigned long hash1 = djb2_hash(str) % table_size;
	unsigned long hash2 = sdbm_hash(str) % table_size;
	return (hash1 + collision_cnt * (hash2 + 1)) % table_size; // hash2 + 1 ->  always not return 0
}

//Init hashmap func
void hashmap_init(hashmap *hash_map, size_t size) {
	hash_map->size = size;
	hash_map->table = malloc(sizeof(cache_entry*) * size);
	for (size_t i = 0; i < size; i++) {
		hash_map->table[i] = NULL;
	}
}

//insert func
void hashmap_insert(hashmap *map, cache_entry *entry) {
	unsigned int collision_cnt = 0;
	unsigned int index;

	do {
		index = double_hashing(entry->url, map->size, collision_cnt);
		collision_cnt++;
	} while (map->table[index] != NULL && collision_cnt < map->size);//충돌시 한바꾸 돌림

	if (collision_cnt < map->size) {
		map->table[index] = entry;
	} else {
		// 삽일 할 곳이 읍을 때 or 해시테이블 공간 부족 시
		hashmap_lru(map);
		hashmap_insert(map, entry); // 2트
	}
}

void hashmap_lru(hashmap *map){
	time_t oldest = time(NULL); //현재는 과거다!
	int oldest_index = -1;

	for (size_t i = 0; i < map->size; i++) {
		if (map->table[i] != NULL && map->table[i]->last_access < oldest) {
			oldest = map->table[i]->last_access;
			oldest_index = i;
		}
	}

	if (oldest_index != -1) {
		// 가장 오래된 거 제거
		free(map->table[oldest_index]->data);
		free(map->table[oldest_index]);
		map->table[oldest_index] = NULL;
	}
}


//search func
cache_entry *hashmap_search(hashmap *map, char *url) {
	unsigned int collision_cnt = 0;
	unsigned int index;

	do {
		index = double_hashing(url, map->size, collision_cnt);
		if (map->table[index] != NULL && strcmp(map->table[index]->url, url) == 0) {
			return map->table[index]; // 찾음
		}
		collision_cnt++;
	} while (map->table[index] != NULL && collision_cnt < map->size);

	return NULL; // 찾지 못함
}

