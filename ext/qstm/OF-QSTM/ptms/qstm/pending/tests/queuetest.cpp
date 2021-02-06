#include <iostream>
#include "../util/queue.hpp"

// Display entire queue
void show(queue_t *q){
	entry *curr = q->head;
	do{
		std::cout << "[" << curr->ts << "] -> ";
		curr = curr->next.load();
	}while(curr != NULL);
	std::cout << "NULL\n";
	if(q->tail.load()->next.load() != NULL) std::cout << "Tail has not been fixed\n";
}

int main(){
	queue_t q;
	entry *foo;

	show(&q);
	for(int i=1; i<10; i++){
		foo = new entry;
		foo->ts = i;
		q.enqueue(foo);
		show(&q);
	}
	while(NULL != q.dequeue()) show(&q);

	return 0;
}
