/* 
 *  Copyright 2021 Alexander Reinert
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *  
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <semaphore.h>

#pragma once

#define SemaphoreHandle_t sem_t

bool sem_wait_timeout(sem_t *sem, int timeout);

#define sem_take(__sem, __timeout) sem_wait_timeout(&__sem, __timeout)
#define sem_give(__sem) sem_post(&__sem)
#define sem_init(__sem) sem_init(&__sem, 1, 0);

void log_frame(const char *text, unsigned char buffer[], uint16_t len);

