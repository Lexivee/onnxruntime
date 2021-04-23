// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#import <XCTest/XCTest.h>

#import "onnxruntime/ort_env.h"

NS_ASSUME_NONNULL_BEGIN

@interface ORTEnvTest : XCTestCase
@end

@implementation ORTEnvTest

- (void)testInitOk {
  ORTEnv* env = [[ORTEnv alloc] initWithError:nil];
  XCTAssertNotNil(env);
}

@end

NS_ASSUME_NONNULL_END
