// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface ORTErrorUtils : NSObject

+ (void)saveErrorCode:(int)code
          description:(const char*)descriptionCstr
              toError:(NSError**)error;

@end

NS_ASSUME_NONNULL_END
