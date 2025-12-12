# Buddy-Memory-Allocation-in-FreeRTOS 💾📜
## Acknowledgments
I would like to express my sincere gratitude to **Prof. Ralf Colmar Staudemeyer (PhD)** for such a wonderful project. I really appriciate your valuable guidance, constructive feedback, and continuous support throughout this project.  
I would also like to thank **Herr. Tobias Tefke** for his assistance, helpful discussions, and technical support during the implementation and evaluation phases of this work.

## The aim of this project is: 🎯
    "FreeRTOS is the operating system that runs on the PineCone. Its memory allocation algorithms mainly 
     base on simple dynamic allocation algorithms. The goal of this proposed project is writing a memory 
     allocator that works according to the buddy memory algorithm. This allocator should be written as 
     extension to the FreeRTOS operating system and serve as replacement for the main memory management
     functions free and malloc. Hence any FreeRTOS applications should be able to run with the allocator
     without changes. Implementing calloc and realloc is a plus."

## ABSTRACT:
     Dynamic memory allocation is an important building block in embedded operating systems with small RAM,
     real-time constraints, and fragmentation which impact system’s reliability. The Buddy Memory Allocation 
     algorithm was originally introduced by Donald Knuth as a power-of-two blocksplitting and coalescing (here 
     it means to join two blocks of memory together) scheme for dynamic storage allocation [6] -provides a 
     theoretically efficient model but has seen only limited adoption within kernels like Linux. In this 
     project, Buddy Memory Allocation for FreeRTOS was designed and implemented such that it extends the FreeRTOS
     memory API- malloc(), free(), calloc() and realloc(). With the goal to offer predictable logarithmic allocation
     time and significantly lowered fragmentation. Our approach extends the traditional buddy system analysis [8] and recent
     augmentations that investigate coalescing efficiency and fragmentation behavior [1], [2]. This memory allocator 
     is designed for resource constrained systems. It exhibits improved memory predictability, higher space utilization, 
     and stable behavior under load compared to the default FreeRTOS heap implementations ((heap_1 to heap_5) )[3]. Prior
     studies of dynamic allocation [11] shows that a well engineered buddy allocator can reduce the gap between theoretical models 
     and practical needs of embedded real-time applications. This project address to fill this gap by providing a practical model
     implementation on PineCone BL602 (FreeRTOS). 
     
### Keywords:Buddy memory allocation algorithm; FreeRTOS; embedded systems; dynamic memory allocation; fragmentation; real-time determinism; block coalescing; performance evaluation; memory management


------------------------------------------------------------
#Main Work
------------------------------------------------------------
<pre>
|
|----- algorithm
|        |----------- heap_buddy.c // code 
|        |----------- concept-explanation.txt // read before understanding heap_buddy.c, review of concepts used in writing heap_buddy.c
|
|----- tests_programms // test cases run for comparing heap_5.c and heap_buddy.c performance
|        |----------- suas_app_buddy_test1.zip // Fragmentation behaviour
|        |----------- suas_app_buddy_test2.zip // Free block merging
|        |----------- suas_app_buddy_test3.zip //  Allocation efficiency
|
| // Contain screenshots of the results 
|----- test1
|----- test2
|----- test3
|
| // Conference paper and Report
|----- G3_Buddy_Conference_Paper.pdf
|----- G3_Report.pdf
</pre>

------------------------------------------------------------
#Installation steps for running heap_buddy.c
------------------------------------------------------------
<pre>
|----- Toolchain
|        |----------- 01_Installation.pdf
|        |----------- 02_Compiling_and_Flashing.pdf
|        |----------- 03_Common_Toolchain_Errors.pdf
|        |----------- 04_SDK_Overview.pdf
|        |----------- 05_Debugging.pdf
|        |----------- 06_WiFi_Sniffer.pdf

| // After installing packages from toolchain -> you can directly paste the heap_buddy.c in the folder structure mentione below. 
| // Paste the heap_buddy.c code in the folder: bl602_iot_sdk/components/freertos/portable/MemMang/ (along with all heap_* files)
|----- ProjectIOT 
|        |----------- bl602_iot_sdk
|                       |----------- ...
</pre>
  
------------------------------------------------------------
#Setting up heap_buddy.c
------------------------------------------------------------
<pre>
| // Now in order to select heap_buddy.c, go the folder : bl602_iot_sdk/components/freertos/bouffalo.mk
| // Now go to COMPONENT_OBJS := $(...), here you see portable/MemMang/heap_bl602.c 
| // change heap_bl602.c to heap_5.c or heap_buddy.c as per test requirement
</pre>
------------------------------------------------------------
#Running FreeRTOS application
------------------------------------------------------------
<pre>
| // Refer to  02_Compiling_and_Flashing.pdf for running programs in PineCone BL602
</pre>

------------------------------------------------------------
