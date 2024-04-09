#!/bin/bash
pfn=$(cat log.log | grep PFN | head -n 1 | cut -d' ' -f2)
echo -n "$pfn "

if [ ${#pfn} -ne 8 ];
	then echo "error" ;
	exit
fi

bits=${pfn: -2}
known_bit=${pfn: -1}

bit0=$(grep -E "(= ${known_bit}300    )|(= ${known_bit}700    )|(= ${known_bit}a80    )|(= ${known_bit}840    )" log.log | awk '{if($4>=300)print;}' | wc -l)
bit1=$(grep -E "(= 1${known_bit}300    )|(= 1${known_bit}700    )|(= 1${known_bit}a80    )|(= 1${known_bit}840    )" log.log | awk '{if($4>=300)print;}' | wc -l)
bit2=$(grep -E "(= 2${known_bit}300    )|(= 2${known_bit}700    )|(= 2${known_bit}a80    )|(= 2${known_bit}840    )" log.log | awk '{if($4>=300)print;}' | wc -l)
bit3=$(grep -E "(= 3${known_bit}300    )|(= 3${known_bit}700    )|(= 3${known_bit}a80    )|(= 3${known_bit}840    )" log.log | awk '{if($4>=300)print;}' | wc -l)


#echo ===================
#echo $bit0
#echo $bit1
#echo $bit2
#echo $bit3
#echo ===================
mymax() {
	if [ $1 -gt $2 ];
		then echo $1; return;
	fi;
	echo $2; return;
}

m=$(mymax "$bit0" "$bit1")
m=$(mymax "$m" "$bit2")
m=$(mymax "$m" "$bit3")

if [ $bit0 -ge $m ];
	then echo 0;
	exit;
fi
if [ $bit1 -ge $m ];
	then echo 1;
	exit;
fi
if [ $bit2 -ge $m ];
	then echo 2;
	exit;
fi
echo 3
