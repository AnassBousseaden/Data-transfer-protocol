
# description breve du projet SURE

#### Sure protocol c'est quoi ?
    SURE est un protocole de transfère de données unidirectionnel de la couche transport utilisant les techniques usuelles pour résister à la perte de données et assurer que les données soient reçus dans le bon ordre.

#### Challenge technique durant le projet 
    - Le protocole sure utilise plusieurs processus pour gérer l'envoie et la réception des paquets, ainsi ces processus utilisent des ressources partager qu'ils ne peuvent pas accéder en même temps.
    - Concevoir un système d'exclusion mutuel (pour les ressources communes) ne menant pas a un "dead lock" est un problème critique pour ce genre de programme. 
    - Lorsque l'on fait un programme multithreadé le débogage (avec gdb par exemple) devient difficile et il est difficile de simplement détecter une erreur.

#### udt
    - Le programme udt.c fournit une interface pour l'envoi de données sans garantie aucune sur la réussite de celle-ci.
    - Il est possible de changer la probabilité d'échec dans le fichier udt.h (PROB_LOSS) pour simuler un réseau avec beaucoup de packet loss.



#### Comment tester sure ?
Notez que le programme affiche beaucoup d'information pour aider à la compréhension du code.

Les programme copy_file et receive_file sont un exemple d'utilisation du protocole SURE pour transférer un fichier d'un hôte (copy_file) vers un autre (receive_file).


Pour tester le protocole avec copy_file et receive_file:
    - générer le makefile avec la commande  **_cmake._** .
    - générer les fichiers **_copy_file_** et **_reveive_file_** avec la commande: **_make copy_file_**;**_make reveive_file_**;
    - Executer la commande : **_./receive_file_**, sur une machine A.
    - Executer la commande **_./copy_file IP(A) file_**, sur une autre machine (A par exemple) ou **IP(A)** est l'adresse ip de A sur le reseau et **file** est le fichier a envoyé.
    Par exemple : **_./copy_file 127.0.0.1 1.send_** pour le faire sur la même machine.

