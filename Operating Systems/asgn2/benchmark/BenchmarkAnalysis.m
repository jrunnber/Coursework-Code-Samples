clear all
close all
clc

data = csvread('sampledata.csv');

count = 1;
total = length(data);
totalTickets = 0;
for i = 1:total
    totalTickets = totalTickets + data(i,6);
    if count == 1
        lotto(count).thread_id = data(i,1);
        lotto(count).count = 0;
        lotto(count).collected = 0;
        lotto(count).percentage = 0;
        count = count + 1;
        continue;
    end
    
    flag = 0;
    for ii = 1 : count-1
        if lotto(ii).thread_id == data(i,1)
            flag = 1;
        end
    end
    
    if flag == 0
        lotto(count).thread_id = data(i,1);
        lotto(count).count = 0;
        lotto(count).collected = 0;
        lotto(count).percentage = 0;
        count = count + 1;
    end
end

%check all transactions see if they equal to thread id total collected
%tickets over
 for ii = 1:(length(data))
     for i = 1:(length(lotto))
        if lotto(i).thread_id == data(ii,1)
            lotto(i).collected = lotto(i).collected + data(ii,2);
            lotto(i).count = lotto(i).count + 1;
            lotto(i).percentage = lotto(i).percentage + data(ii,2)/data(ii,6);
        end
    end
 end

%Create arrays for bar graphs
for i = 1:(length(lotto))
    actual(i) = lotto(i).count/length(data);
    percentages(i) = lotto(i).collected/totalTickets;
    tickets(i) = lotto(i).collected;
end


subplot(311);
bar(actual)
title('Actual');
ylabel('Winning Percentage');
xlabel('Process ID');
subplot(312);
bar(percentages)
title('Calculated');
xlabel('Process ID');
ylabel('Winning Percentage');
subplot(313);
bar(tickets)
title('Tickets Collected');
xlabel('Process ID');
ylabel('Tickets');





 
