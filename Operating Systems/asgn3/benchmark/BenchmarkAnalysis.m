clear all
close all
clc

filename = 'data.txt';

fid = fopen(filename);
data = textscan(fid,'%*s %*s %d %*s %d %*s %d','Delimiter',' ');
fclose(fid);

queue = data{1};
oldest = data{2};
newest = data{3};

scatter(queue,oldest);
xlabel('Queue Size');
ylabel('Oldest[s]');
title('Queue Size vs Oldest')
figure
time = linspace(1,length(oldest),length(oldest));

subplot(311);
plot(time,queue);
xlabel('Pageout Scans');
ylabel('Queue Size');
title('Queue Size (Sample Run)')

subplot(312);
plot(time,oldest);
xlabel('Pageout Scans');
ylabel('Oldest[s]');
title('Oldest (Sample Run)')


subplot(313);
plot(time,newest);
xlabel('Pageout Scans');
ylabel('Newest[s]');
title('Newest (Sample Run)')





 