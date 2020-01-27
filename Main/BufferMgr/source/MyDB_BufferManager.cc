
#ifndef BUFFER_MGR_C
#define BUFFER_MGR_C

#include "MyDB_BufferManager.h"
#include <MyDB_Page.h>
#include <MyDB_PageHandle.h>
#include <stdlib.h>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>

using namespace std;

void MyDB_BufferManager :: updateLRU(MyDB_PagePtr newPage){
    //remove
    if(newPage->next!= nullptr && newPage->prev != nullptr){//in LRU
        newPage->prev->next = newPage->next;
        newPage->next->prev = newPage->prev;
        cout<<"remove\n";
    }
    //insert to head;
    cout<<"insert to head\n";
    newPage->next = this->head->next;
    this->head->next = newPage;
    cout<<newPage->next<<endl;
    newPage->next->prev = newPage;
    newPage->prev = this->head;
}

void MyDB_BufferManager :: readFromDisk(MyDB_PagePtr newPage){
    //get available buffer and assign
    void* availableBuffer = getAvailableBuffer();
    newPage->setPageAddr(availableBuffer);
    newPage->setIsInBuffer(true);

    //read data from disk to buffer
    int fd = open (newPage->getWhichTable()->getStorageLoc().c_str (),    O_CREAT | O_RDWR | O_SYNC, 0666);
    lseek (fd, newPage -> getOffset() * pageSize, SEEK_SET);
    write (fd, newPage -> getPageAddr(), pageSize);
}

MyDB_PageHandle MyDB_BufferManager :: getPage (MyDB_TablePtr whichTable, long pageNum) {
    //check whether the page has already been in buffer.

    pair<string, long>thisPage = make_pair(whichTable->getName(),pageNum);
    auto it = this->currentPages.find(thisPage);

    MyDB_PageHandle new_handle;
    //not found, allocate a page
    if(it == this->currentPages.end()){
        MyDB_PagePtr new_page = make_shared <MyDB_Page>(nullptr,whichTable,pageNum);
        new_handle = make_shared<MyDB_PageHandleBase>(new_page, this) ;
        currentPages[thisPage] = new_page;
    }else{//found, create a corresponding handle
        new_handle = make_shared<MyDB_PageHandleBase>(it->second,this);
    }
    return new_handle;
}

MyDB_PageHandle MyDB_BufferManager :: getPage () {
    size_t filePos = *this->tempFilePos.begin();
    this->tempFilePos.erase(filePos);
    if(this->tempFilePos.empty()){
        this->tempFilePos.insert(filePos+1);
    }

    MyDB_PagePtr new_page = make_shared <MyDB_Page>(nullptr,nullptr,filePos);
    MyDB_PageHandle new_handler = make_shared<MyDB_PageHandleBase>(new_page,this);

    this->currentPages[make_pair(nullptr,filePos)] = new_handler->mPage;
    new_handler->mPage->setIsInBuffer(true);
    new_handler->mPage->setIsAnonymous(true);
    return new_handler;
}

void * MyDB_BufferManager :: getAvailableBuffer () {
    // Check queue first. if available, return a page
    if ( ! this -> availBufferPool.empty() ) {
        cout<<"fetch buffer from buffer pool\n";
        void * availAddr = this -> availBufferPool.front();
        availBufferPool.pop();
        return availAddr;
    } else {
        // If non available buffer in queue, choose a least recently used unpinned page from LRU.
        MyDB_PagePtr curr = this -> rear -> prev;
        while (curr && curr->getIsPinned()) {
            curr = curr -> prev;
        }
        // all pages are pinned
        if (curr == this->head || !curr) {
            printf("No available memory");
            exit(1);
        }
        // found a least recently used unpinned page  -3=2=1p=0p-NULL 2=3=1p=0p-NULL
        if (curr->getIsDirty()) {
            // write back dirty page
            if(curr->getIsAnonymous()){
                int fd = open (tempFile.c_str (),  O_SYNC | O_CREAT | O_RDWR, 0666);
                lseek (fd, curr -> getOffset() * pageSize, SEEK_SET);
                write (fd, curr -> getPageAddr(), pageSize);
            }
            else{
                int fd = open (curr->getWhichTable()->getStorageLoc().c_str (),  O_SYNC | O_CREAT | O_RDWR, 0666);
                lseek (fd, curr -> getOffset() * pageSize, SEEK_SET);
                write (fd, curr -> getPageAddr(), pageSize);
            }
            curr -> setIsDirty(false);
        }
        MyDB_PagePtr prev = curr -> prev;  //  3
        MyDB_PagePtr next = curr -> next; // 1p
        prev -> next = next; // 3->1 3<-2<->1p
        next -> prev = prev; // 3<->1 3<-2->1p
        curr -> next = nullptr;
        curr -> prev = nullptr;
        curr->setIsInBuffer(false);
        void * availAddr = curr -> getPageAddr();
        return availAddr;
    }
}

MyDB_PageHandle MyDB_BufferManager :: getPinnedPage (MyDB_TablePtr whichTable, long pageNum) {

    pair<string, long>thisPage = make_pair(whichTable->getName(),pageNum);
    auto it = this->currentPages.find(thisPage);
    MyDB_PageHandle new_handle;

    //not found in currentPages

    if(it == this->currentPages.end()){
        void* availableBuffer = getAvailableBuffer();
        cout<<"fetch success\n";
        MyDB_PagePtr new_page = make_shared <MyDB_Page>(availableBuffer,whichTable,pageNum);
        cout<<"page success\n";
        new_handle = make_shared<MyDB_PageHandleBase>(new_page,this);
        currentPages[thisPage] = new_page;
        cout<<"not found but create pinned page\n";
    }else{//found in currentPages
        if(it->second->getIsInBuffer()){//Already in Buffer
            new_handle = make_shared<MyDB_PageHandleBase>(it->second, this);
        }else{//In disk
            new_handle = make_shared<MyDB_PageHandleBase>(it->second, this);
            void* availableBuffer = getAvailableBuffer();
            new_handle->mPage->setPageAddr(availableBuffer);
        }
    }
    //set attribute;
    new_handle->mPage->setIsInBuffer(true);
    new_handle->mPage->setIsPinned(true);
    cout<<"begin update\n";
    updateLRU(new_handle->mPage);
    cout<<"finish update\n";
	return new_handle;
}

MyDB_PageHandle MyDB_BufferManager :: getPinnedPage () {
    void* availableBuffer = getAvailableBuffer();
    MyDB_PageHandle new_handle = getPage();
    new_handle->mPage->setPageAddr(availableBuffer);
    new_handle->mPage->setIsPinned(true);
	return new_handle;
}

void MyDB_BufferManager :: unpin (MyDB_PageHandle unpinMe) {
    unpinMe->mPage->setIsPinned(false);
}

MyDB_BufferManager :: MyDB_BufferManager (size_t pageSize, size_t numPages, string tempFile) {

    //Initialize some attributes.
    this->pageSize = pageSize;
    this->numPages = numPages;
    this->tempFile = tempFile;

    //Initialize linked list.
    this->head = make_shared<MyDB_Page>(nullptr, nullptr,0);//dummy node
    this->rear = make_shared<MyDB_Page>(nullptr, nullptr,0);//dummy node
    this->head->next = this->rear;
    this->head->prev = nullptr;
    this->rear->prev = this->head;
    this->rear->next = nullptr;

    this->tempFilePos.insert(0);

    //allocate memory at once.
    for(int i = 0; i < this->numPages; i++){
        void* new_addr = malloc(this->pageSize);
        if(new_addr == NULL){
            printf("Allocation failed...");
            exit(1);
        }
        this->availBufferPool.push(new_addr);
    }
}


void MyDB_BufferManager::releaseMemory(MyDB_PagePtr releasePage) {
    // find releasePage
    auto tmpPair = make_pair(releasePage->getWhichTable()->getName(), releasePage->getOffset());
    auto it = currentPages.find(tmpPair);
    if (it != currentPages.end()) {
        // found it
        //MyDB_Page* myPage = currentPages[tmpPair];
        //ready to release

        // if it is pinned
        auto curr = this -> rear -> prev;
        while (curr) {
            if (curr->getWhichTable() == releasePage->getWhichTable() && curr->getOffset() == releasePage->getOffset()) {
                break;
            }
            curr = curr -> prev;
        }
        //MyDB_PageHandle tmp = make_shared <MyDB_PageHandleBase> (releasePage);
        if (releasePage->getPageAddr() && curr && curr->getIsPinned()) {
            // unpin it
            releasePage->setIsPinned(false);
            updateLRU(releasePage);
            return;
        }

        // if it is anonymous
        if(releasePage->getIsAnonymous()) {
            // if it is dirty
            if (releasePage->getIsDirty()) {
                // write back dirty page
                int fd = open(tempFile.c_str(), O_TRUNC | O_CREAT | O_RDWR, 0666);
                lseek(fd, releasePage->getOffset() * pageSize, SEEK_SET);
                write(fd, releasePage->getPageAddr(), pageSize);
                releasePage -> setIsDirty(false);
            }
            // remove slot
            tempFilePos.insert(releasePage->getOffset());
            // find it in LRU
            curr = this -> rear -> prev;
            while (curr) {
                if (curr->getWhichTable() == releasePage->getWhichTable() && curr->getOffset() == releasePage->getOffset()) {
                    break;
                }
                curr = curr -> prev;
            }

            // remove from LRU
            if (curr && (curr->next || curr->prev) ) {
                curr->next->prev = curr->prev;
                curr->prev->next = curr->next;
                curr.reset();
            }

            // release memory
            if (releasePage -> getPageAddr()) {
                availBufferPool.push(releasePage->getPageAddr());
                releasePage->setPageAddr(nullptr);
            }
        }
    }
}

MyDB_BufferManager :: ~MyDB_BufferManager () {
    MyDB_PagePtr cur = head;
    while(cur!= nullptr){
        free(cur->getPageAddr());
        MyDB_PagePtr temp = cur;
        cur = cur->next;
        temp.reset();
    }
    while(!availBufferPool.empty()){
        free(availBufferPool.front());
        availBufferPool.pop();
    }
}

#endif


