#include <iostream>

class Node {
public:
    int data;
    Node* next;

    // Constructor with initialization list
    Node(int val) : data(val), next(nullptr) {}
};

int main() {
    // Dynamic memory allocation
    Node* head = new Node(100);
    head->next = new Node(200);

    Node* temp = head;
    while (temp != nullptr) {
        std::cout << temp->data << " -> ";
        temp = temp->next;
    }
    
    // Cleanup
    delete head->next;
    delete head;

    return 0;
}