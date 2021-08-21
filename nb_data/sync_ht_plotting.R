# Copyright 2015 University of Rochester
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
# http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License. 


library(plyr)
library(ggplot2)

scientific_10 <- function(x) {
  parse(text=gsub("1e\\+", "10^", scales::scientific_format()(x)))
}
tests<-c("g50i25r25_sync")
for (t in tests){
read.csv(paste("./",t,".csv",sep=""))->montagedata

montagedata$ds<-as.factor(gsub("PLockfreeHashTable","Izraelevitz",montagedata$ds))
montagedata$ds<-as.factor(gsub("nbMontageLfHashTable","nbMontage",montagedata$ds))
montagedata$ds<-as.factor(gsub("MontageLfHashTable","Montage",montagedata$ds))
montagedata$ds<-as.factor(gsub("LfHashTable","DRAM (T)",montagedata$ds))
montagedata$ds<-as.factor(gsub("Dali","Dalí",montagedata$ds))
montagedata$ds<-as.factor(gsub("NVMSOFT","NVMSOFT",montagedata$ds))
montagedata$ds<-as.factor(gsub("SOFT","SOFT",montagedata$ds))
montagedata$ds<-as.factor(gsub("NVTraverseHashTable","NVTraverse",montagedata$ds))
montagedata$ds<-as.factor(gsub("CLevelHashTable","CLevel",montagedata$ds))
d1<-subset(montagedata,ds=="DRAM (T)")
d2<-subset(montagedata,ds=="nbMontage")
d3<-subset(montagedata,ds=="Montage")
d4<-subset(montagedata,ds=="Izraelevitz")
d5<-subset(montagedata,ds=="Dalí")
d6<-subset(montagedata,ds=="SOFT")
d7<-subset(montagedata,ds=="NVMSOFT")
d8<-subset(montagedata,ds=="NVTraverse")
d9<-subset(montagedata,ds=="CLevel")
lkdata = rbind(d1,d2,d3,d4,d5,d6,d7,d8,d9)

ddply(.data=lkdata,.(ds,sync),mutate,mops= mean(ops)/1000000)->lkdata
lindata = rbind(lkdata[,c("ds","sync","mops")])
lindata$ds <- factor(lindata$ds, levels=c("DRAM (T)", "nbMontage", "Montage", "Izraelevitz", "NVTraverse", "NVMSOFT", "SOFT", "CLevel", "Dalí"))

# Set up colors and shapes (invariant for all plots)
color_key = c("#12E1EA","#C11B14","#1245EA",
              "#FF69B4","#809900","#1BC40F",
              "#660099","#5947ff",
              "#FF8C00", "#F86945",
              "#191970")
names(color_key) <- levels(lindata$ds)

shape_key = c(2,18,4,19,62,17,0,15,1)
names(shape_key) <- levels(lindata$ds)

line_key = c(2,1,1,1,1,1,1,1,1)
names(line_key) <- levels(lindata$ds)

# legend_pos=c(0.5,0.92)
# y_range_up = 2000

# Benchmark-specific plot formatting
legend_pos=c(0.52,0.8)
# y_range_up=300
y_name="Throughput (Mops/s)"
y_range_down = 0
# if(t=="g0i50r50_thread")
#   y_range_up = 18
# else if (t=="g50i25r25_thread")
#   y_range_up = 22
# else
  y_range_up = 22

# Generate the plots
linchart<-ggplot(data=lindata,
                  aes(x=sync,y=mops,color=ds, shape=ds, linetype=ds))+
  geom_line()+xlab("op/sync")+ylab(y_name)+geom_point(size=4)+
  scale_shape_manual(values=shape_key[names(shape_key) %in% lindata$ds])+
  scale_linetype_manual(values=line_key[names(line_key) %in% lindata$ds])+
  theme_bw()+ guides(shape=guide_legend(title=NULL,ncol=2))+ 
  guides(color=guide_legend(title=NULL,ncol=2))+
  guides(linetype=guide_legend(title=NULL,ncol=2))+
  scale_color_manual(values=color_key[names(color_key) %in% lindata$ds])+
  scale_x_continuous(trans='log2',label=scientific_10,breaks=c(1,10,100,1000,10000,100000,1000000),
      minor_breaks=c(2,3,4,5,6,7,8,9,20,30,40,50,60,70,80,90,200,300,400,500,600,700,800,900,2000,3000,4000,5000,6000,7000,8000,9000,20000,30000,40000,50000,60000,70000,80000,90000,200000,300000,400000,500000,600000,700000,800000,900000))+
  # scale_y_continuous(trans='log2',label=scientific_10,breaks=c(10000,100000,1000000,1000000,10000000,100000000),
  #               minor_breaks=c(20000,30000,40000,50000,60000,70000,80000,90000,200000,300000,400000,500000,600000,700000,800000,900000,2000000,3000000,4000000,5000000,6000000,7000000,8000000,9000000,20000000,30000000,40000000,50000000,60000000,70000000,80000000,90000000,200000000,300000000,400000000,500000000,600000000,700000000,800000000,900000000,2000000000))+
  coord_cartesian(xlim = c(0.3, 1000000), ylim = c(y_range_down,y_range_up))+
  theme(plot.margin = unit(c(.2,0,-1.5,0), "cm"))+
  theme(
    legend.position=legend_pos,
    legend.background = element_blank(),
    legend.key = element_rect(colour = NA, fill = "transparent"))+
  theme(text = element_text(size = 18))+
  theme(axis.title.y = element_text(margin = margin(t = 0, r = 5, b = 0, l = 2)))+
  theme(axis.title.x = element_text(hjust =-0.24,vjust = 13.5,margin = margin(t = 15, r = 0, b = 10, l = 0)))

# Save all four plots to separate PDFs
ggsave(filename = paste("./hashtables_",t,".pdf",sep=""),linchart,width=4, height = 4, units = "in", dpi=300, title = paste("hashtables_",t,".pdf",sep=""))
}
# width=15.95
