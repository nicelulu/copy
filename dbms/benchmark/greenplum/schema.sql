CREATE TABLE hits_all_10m ( WatchID bigint, JavaEnable int, Title text, GoodEvent int, EventTime timestamp, EventDate timestamp, CounterID bigint, ClientIP bigint, RegionID bigint, UserID bigint, CounterClass int, OS int, UserAgent int, URL text, Referer text, Refresh int, RefererCategoryID int, RefererRegionID bigint, URLCategoryID int, URLRegionID bigint, ResolutionWidth int, ResolutionHeight int, ResolutionDepth int, FlashMajor int, FlashMinor int, FlashMinor2 text, NetMajor int, NetMinor int, UserAgentMajor int, CookieEnable int, JavascriptEnable int, IsMobile int, MobilePhone int, MobilePhoneModel text, Params text, IPNetworkID bigint, TraficSourceID int, SearchEngineID int, SearchPhrase text, AdvEngineID int, IsArtifical int, WindowClientWidth int, WindowClientHeight int, ClientTimeZone int, ClientEventTime timestamp, SilverlightVersion1 int, SilverlightVersion2 int, SilverlightVersion3 bigint, SilverlightVersion4 int, PageCharset text, CodeVersion bigint, IsLink int, IsDownload int, IsNotBounce int, FUniqID bigint, OriginalURL text, HID bigint, IsOldCounter int, IsEvent int, IsParameter int, DontCountHits int, WithHash int, HitColor varchar(3), LocalEventTime timestamp, Age int, Sex int, Income int, Interests int, Robotness int, RemoteIP bigint, WindowName int, OpenerName int, HistoryLength int, SocialNetwork text, SocialAction text, HTTPError int, SendTiming bigint, DNSTiming bigint, ConnectTiming bigint, ResponseStartTiming bigint, ResponseEndTiming bigint, FetchTiming bigint, SocialSourceNetworkID int, SocialSourcePage text, ParamPrice int, ParamOrderID text, OpenstatServiceName text, OpenstatCampaignID text, OpenstatAdID text, OpenstatSourceID text, UTMSource text, UTMMedium text, UTMCampaign text, UTMContent text, UTMTerm text, FromTag text, HasGCLID int, RefererHash bigint, URLHash bigint, CLID bigint) WITH (appendonly=true, orientation=column, compresstype=quicklz) DISTRIBUTED BY (userid) ;
CREATE TABLE hits_all_100m ( WatchID bigint, JavaEnable int, Title text, GoodEvent int, EventTime timestamp, EventDate timestamp, CounterID bigint, ClientIP bigint, RegionID bigint, UserID bigint, CounterClass int, OS int, UserAgent int, URL text, Referer text, Refresh int, RefererCategoryID int, RefererRegionID bigint, URLCategoryID int, URLRegionID bigint, ResolutionWidth int, ResolutionHeight int, ResolutionDepth int, FlashMajor int, FlashMinor int, FlashMinor2 text, NetMajor int, NetMinor int, UserAgentMajor int, CookieEnable int, JavascriptEnable int, IsMobile int, MobilePhone int, MobilePhoneModel text, Params text, IPNetworkID bigint, TraficSourceID int, SearchEngineID int, SearchPhrase text, AdvEngineID int, IsArtifical int, WindowClientWidth int, WindowClientHeight int, ClientTimeZone int, ClientEventTime timestamp, SilverlightVersion1 int, SilverlightVersion2 int, SilverlightVersion3 bigint, SilverlightVersion4 int, PageCharset text, CodeVersion bigint, IsLink int, IsDownload int, IsNotBounce int, FUniqID bigint, OriginalURL text, HID bigint, IsOldCounter int, IsEvent int, IsParameter int, DontCountHits int, WithHash int, HitColor varchar(3), LocalEventTime timestamp, Age int, Sex int, Income int, Interests int, Robotness int, RemoteIP bigint, WindowName int, OpenerName int, HistoryLength int, SocialNetwork text, SocialAction text, HTTPError int, SendTiming bigint, DNSTiming bigint, ConnectTiming bigint, ResponseStartTiming bigint, ResponseEndTiming bigint, FetchTiming bigint, SocialSourceNetworkID int, SocialSourcePage text, ParamPrice int, ParamOrderID text, OpenstatServiceName text, OpenstatCampaignID text, OpenstatAdID text, OpenstatSourceID text, UTMSource text, UTMMedium text, UTMCampaign text, UTMContent text, UTMTerm text, FromTag text, HasGCLID int, RefererHash bigint, URLHash bigint, CLID bigint) WITH (appendonly=true, orientation=column, compresstype=quicklz) DISTRIBUTED BY (userid) ;
CREATE TABLE hits_all_1000m ( WatchID bigint, JavaEnable int, Title text, GoodEvent int, EventTime timestamp, EventDate timestamp, CounterID bigint, ClientIP bigint, RegionID bigint, UserID bigint, CounterClass int, OS int, UserAgent int, URL text, Referer text, Refresh int, RefererCategoryID int, RefererRegionID bigint, URLCategoryID int, URLRegionID bigint, ResolutionWidth int, ResolutionHeight int, ResolutionDepth int, FlashMajor int, FlashMinor int, FlashMinor2 text, NetMajor int, NetMinor int, UserAgentMajor int, CookieEnable int, JavascriptEnable int, IsMobile int, MobilePhone int, MobilePhoneModel text, Params text, IPNetworkID bigint, TraficSourceID int, SearchEngineID int, SearchPhrase text, AdvEngineID int, IsArtifical int, WindowClientWidth int, WindowClientHeight int, ClientTimeZone int, ClientEventTime timestamp, SilverlightVersion1 int, SilverlightVersion2 int, SilverlightVersion3 bigint, SilverlightVersion4 int, PageCharset text, CodeVersion bigint, IsLink int, IsDownload int, IsNotBounce int, FUniqID bigint, OriginalURL text, HID bigint, IsOldCounter int, IsEvent int, IsParameter int, DontCountHits int, WithHash int, HitColor varchar(3), LocalEventTime timestamp, Age int, Sex int, Income int, Interests int, Robotness int, RemoteIP bigint, WindowName int, OpenerName int, HistoryLength int, SocialNetwork text, SocialAction text, HTTPError int, SendTiming bigint, DNSTiming bigint, ConnectTiming bigint, ResponseStartTiming bigint, ResponseEndTiming bigint, FetchTiming bigint, SocialSourceNetworkID int, SocialSourcePage text, ParamPrice int, ParamOrderID text, OpenstatServiceName text, OpenstatCampaignID text, OpenstatAdID text, OpenstatSourceID text, UTMSource text, UTMMedium text, UTMCampaign text, UTMContent text, UTMTerm text, FromTag text, HasGCLID int, RefererHash bigint, URLHash bigint, CLID bigint) WITH (appendonly=true, orientation=column,compresstype=quicklz) DISTRIBUTED BY (userid) ;
